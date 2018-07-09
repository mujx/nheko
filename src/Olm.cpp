#include "Olm.hpp"

#include "Cache.h"
#include "Logging.hpp"
#include "MatrixClient.h"

using namespace mtx::crypto;

static const std::string STORAGE_SECRET_KEY("secret");
constexpr auto MEGOLM_ALGO = "m.megolm.v1.aes-sha2";

namespace {
auto client_ = std::make_unique<mtx::crypto::OlmClient>();
}

namespace olm {

mtx::crypto::OlmClient *
client()
{
        return client_.get();
}

void
handle_to_device_messages(const std::vector<nlohmann::json> &msgs)
{
        if (msgs.empty())
                return;

        nhlog::crypto()->info("received {} to_device messages", msgs.size());

        for (const auto &msg : msgs) {
                if (msg.count("type") == 0) {
                        nhlog::crypto()->warn("received message with no type field: {}",
                                              msg.dump(2));
                        continue;
                }

                std::string msg_type = msg.at("type");

                if (msg_type == to_string(mtx::events::EventType::RoomEncrypted)) {
                        try {
                                OlmMessage olm_msg = msg;
                                handle_olm_message(std::move(olm_msg));
                        } catch (const nlohmann::json::exception &e) {
                                nhlog::crypto()->warn(
                                  "parsing error for olm message: {} {}", e.what(), msg.dump(2));
                        } catch (const std::invalid_argument &e) {
                                nhlog::crypto()->warn(
                                  "validation error for olm message: {} {}", e.what(), msg.dump(2));
                        }

                } else if (msg_type == to_string(mtx::events::EventType::RoomKeyRequest)) {
                        nhlog::crypto()->warn("handling key request event: {}", msg.dump(2));
                        try {
                                mtx::events::msg::KeyRequest req = msg;
                                if (req.action == mtx::events::msg::RequestAction::Request)
                                        handle_key_request_message(std::move(req));
                                else
                                        nhlog::crypto()->warn(
                                          "ignore key request (unhandled action): {}",
                                          req.request_id);
                        } catch (const nlohmann::json::exception &e) {
                                nhlog::crypto()->warn(
                                  "parsing error for key_request message: {} {}",
                                  e.what(),
                                  msg.dump(2));
                        }
                } else {
                        nhlog::crypto()->warn("unhandled event: {}", msg.dump(2));
                }
        }
}

void
handle_olm_message(const OlmMessage &msg)
{
        nhlog::crypto()->info("sender    : {}", msg.sender);
        nhlog::crypto()->info("sender_key: {}", msg.sender_key);

        const auto my_key = olm::client()->identity_keys().curve25519;

        for (const auto &cipher : msg.ciphertext) {
                // We skip messages not meant for the current device.
                if (cipher.first != my_key)
                        continue;

                const auto type = cipher.second.type;
                nhlog::crypto()->info("type: {}", type == 0 ? "OLM_PRE_KEY" : "OLM_MESSAGE");

                auto payload = try_olm_decryption(msg.sender_key, cipher.second);

                if (payload) {
                        nhlog::crypto()->info("decrypted olm payload: {}", payload.value().dump(2));
                        create_inbound_megolm_session(msg.sender, msg.sender_key, payload.value());
                        return;
                }

                // Not a PRE_KEY message
                if (cipher.second.type != 0) {
                        // TODO: log that it should have matched something
                        return;
                }

                handle_pre_key_olm_message(msg.sender, msg.sender_key, cipher.second);
        }
}

void
handle_pre_key_olm_message(const std::string &sender,
                           const std::string &sender_key,
                           const mtx::events::msg::OlmCipherContent &content)
{
        nhlog::crypto()->info("opening olm session with {}", sender);

        OlmSessionPtr inbound_session = nullptr;
        try {
                inbound_session =
                  olm::client()->create_inbound_session_from(sender_key, content.body);

                // We also remove the one time key used to establish that
                // session so we'll have to update our copy of the account object.
                cache::client()->saveOlmAccount(olm::client()->save("secret"));
        } catch (const olm_exception &e) {
                nhlog::crypto()->critical(
                  "failed to create inbound session with {}: {}", sender, e.what());
                return;
        }

        if (!matches_inbound_session_from(inbound_session.get(), sender_key, content.body)) {
                nhlog::crypto()->warn("inbound olm session doesn't match sender's key ({})",
                                      sender);
                return;
        }

        mtx::crypto::BinaryBuf output;
        try {
                output =
                  olm::client()->decrypt_message(inbound_session.get(), content.type, content.body);
        } catch (const olm_exception &e) {
                nhlog::crypto()->critical(
                  "failed to decrypt olm message {}: {}", content.body, e.what());
                return;
        }

        auto plaintext = json::parse(std::string((char *)output.data(), output.size()));
        nhlog::crypto()->info("decrypted message: \n {}", plaintext.dump(2));

        try {
                cache::client()->saveOlmSession(sender_key, std::move(inbound_session));
        } catch (const lmdb::error &e) {
                nhlog::db()->warn(
                  "failed to save inbound olm session from {}: {}", sender, e.what());
        }

        create_inbound_megolm_session(sender, sender_key, plaintext);
}

mtx::events::msg::Encrypted
encrypt_group_message(const std::string &room_id,
                      const std::string &device_id,
                      const std::string &body)
{
        using namespace mtx::events;

        // Always chech before for existence.
        auto res     = cache::client()->getOutboundMegolmSession(room_id);
        auto payload = olm::client()->encrypt_group_message(res.session, body);

        // Prepare the m.room.encrypted event.
        msg::Encrypted data;
        data.ciphertext = std::string((char *)payload.data(), payload.size());
        data.sender_key = olm::client()->identity_keys().curve25519;
        data.session_id = res.data.session_id;
        data.device_id  = device_id;
        data.algorithm  = MEGOLM_ALGO;

        auto message_index = olm_outbound_group_session_message_index(res.session);
        nhlog::crypto()->info("next message_index {}", message_index);

        // We need to re-pickle the session after we send a message to save the new message_index.
        cache::client()->updateOutboundMegolmSession(room_id, message_index);

        return data;
}

boost::optional<json>
try_olm_decryption(const std::string &sender_key, const mtx::events::msg::OlmCipherContent &msg)
{
        auto session_ids = cache::client()->getOlmSessions(sender_key);

        nhlog::crypto()->info("attempt to decrypt message with {} known session_ids",
                              session_ids.size());

        for (const auto &id : session_ids) {
                auto session = cache::client()->getOlmSession(sender_key, id);

                if (!session)
                        continue;

                mtx::crypto::BinaryBuf text;

                try {
                        text = olm::client()->decrypt_message(session->get(), msg.type, msg.body);
                        cache::client()->saveOlmSession(id, std::move(session.value()));

                } catch (const olm_exception &e) {
                        nhlog::crypto()->info("failed to decrypt olm message ({}, {}) with {}: {}",
                                              msg.type,
                                              sender_key,
                                              id,
                                              e.what());
                        continue;
                } catch (const lmdb::error &e) {
                        nhlog::crypto()->critical("failed to save session: {}", e.what());
                        return {};
                }

                try {
                        return json::parse(std::string((char *)text.data(), text.size()));
                } catch (const json::exception &e) {
                        nhlog::crypto()->critical("failed to parse the decrypted session msg: {}",
                                                  e.what());
                }
        }

        return {};
}

void
create_inbound_megolm_session(const std::string &sender,
                              const std::string &sender_key,
                              const nlohmann::json &payload)
{
        std::string room_id, session_id, session_key;

        try {
                room_id     = payload.at("content").at("room_id");
                session_id  = payload.at("content").at("session_id");
                session_key = payload.at("content").at("session_key");
        } catch (const nlohmann::json::exception &e) {
                nhlog::crypto()->critical(
                  "failed to parse plaintext olm message: {} {}", e.what(), payload.dump(2));
                return;
        }

        MegolmSessionIndex index;
        index.room_id    = room_id;
        index.session_id = session_id;
        index.sender_key = sender_key;

        try {
                auto megolm_session = olm::client()->init_inbound_group_session(session_key);
                cache::client()->saveInboundMegolmSession(index, std::move(megolm_session));
        } catch (const lmdb::error &e) {
                nhlog::crypto()->critical("failed to save inbound megolm session: {}", e.what());
                return;
        } catch (const olm_exception &e) {
                nhlog::crypto()->critical("failed to create inbound megolm session: {}", e.what());
                return;
        }

        nhlog::crypto()->info("established inbound megolm session ({}, {})", room_id, sender);
}

void
mark_keys_as_published()
{
        olm::client()->mark_keys_as_published();
        cache::client()->saveOlmAccount(olm::client()->save(STORAGE_SECRET_KEY));
}

void
request_keys(const std::string &room_id, const std::string &event_id)
{
        nhlog::crypto()->info("requesting keys for event {} at {}", event_id, room_id);

        http::v2::client()->get_event(
          room_id,
          event_id,
          [event_id, room_id](const mtx::events::collections::TimelineEvents &res,
                              mtx::http::RequestErr err) {
                  using namespace mtx::events;

                  if (err) {
                          nhlog::net()->warn(
                            "failed to retrieve event {} from {}", event_id, room_id);
                          return;
                  }

                  if (!mpark::holds_alternative<EncryptedEvent<msg::Encrypted>>(res)) {
                          nhlog::net()->info(
                            "retrieved event is not encrypted: {} from {}", event_id, room_id);
                          return;
                  }

                  olm::send_key_request_for(room_id,
                                            mpark::get<EncryptedEvent<msg::Encrypted>>(res));
          });
}

void
send_key_request_for(const std::string &room_id,
                     const mtx::events::EncryptedEvent<mtx::events::msg::Encrypted> &e)
{
        using namespace mtx::events;

        nhlog::crypto()->debug("sending key request: {}", json(e).dump(2));
        auto payload = json{{"action", "request"},
                            {"request_id", http::v2::client()->generate_txn_id()},
                            {"requesting_device_id", http::v2::client()->device_id()},
                            {"body",
                             {{"algorithm", MEGOLM_ALGO},
                              {"room_id", room_id},
                              {"sender_key", e.content.sender_key},
                              {"session_id", e.content.session_id}}}};

        json body;
        body["messages"][e.sender]                      = json::object();
        body["messages"][e.sender][e.content.device_id] = payload;

        nhlog::crypto()->debug("m.room_key_request: {}", body.dump(2));

        http::v2::client()->send_to_device(
          "m.room_key_request", body, [e](mtx::http::RequestErr err) {
                  if (err) {
                          nhlog::net()->warn("failed to send "
                                             "send_to_device "
                                             "message: {}",
                                             err->matrix_error.error);
                  }

                  nhlog::net()->info(
                    "m.room_key_request sent to {}:{}", e.sender, e.content.device_id);
          });
}

void
handle_key_request_message(const mtx::events::msg::KeyRequest &req)
{
        if (req.algorithm != MEGOLM_ALGO) {
                nhlog::crypto()->info("ignoring key request {} with invalid algorithm: {}",
                                      req.request_id,
                                      req.algorithm);
                return;
        }

        // Check if we were the sender of the session being requested.
        if (req.sender_key != olm::client()->identity_keys().curve25519) {
                nhlog::crypto()->info("ignoring key request {} because we were not the sender: "
                                      "\nrequested({}) ours({})",
                                      req.request_id,
                                      req.sender_key,
                                      olm::client()->identity_keys().curve25519);
                return;
        }

        // Check if we have the keys for the requested session.
        if (!cache::client()->outboundMegolmSessionExists(req.room_id)) {
                nhlog::crypto()->warn("requested session not found in room: {}", req.room_id);
                return;
        }

        // Check that the requested session_id and the one we have saved match.
        const auto session = cache::client()->getOutboundMegolmSession(req.room_id);
        if (req.session_id != session.data.session_id) {
                nhlog::crypto()->warn("session id of retrieved session doesn't match the request: "
                                      "requested({}), ours({})",
                                      req.session_id,
                                      session.data.session_id);
                return;
        }

        //
        // Prepare the m.room_key event.
        //
        auto payload = json{{"algorithm", "m.megolm.v1.aes-sha2"},
                            {"room_id", req.room_id},
                            {"session_id", req.session_id},
                            {"session_key", session.data.session_key}};

        send_megolm_key_to_device(req.sender, req.requesting_device_id, payload);
}

void
send_megolm_key_to_device(const std::string &user_id,
                          const std::string &device_id,
                          const json &payload)
{
        mtx::requests::QueryKeys req;
        req.device_keys[user_id] = {device_id};

        http::v2::client()->query_keys(
          req,
          [payload, user_id, device_id](const mtx::responses::QueryKeys &res,
                                        mtx::http::RequestErr err) {
                  if (err) {
                          nhlog::net()->warn("failed to query device keys: {} {}",
                                             err->matrix_error.error,
                                             static_cast<int>(err->status_code));
                          return;
                  }

                  nhlog::net()->warn("retrieved device keys from {}, {}", user_id, device_id);

                  if (res.device_keys.empty()) {
                          nhlog::net()->warn("no devices retrieved {}", user_id);
                          return;
                  }

                  auto device = res.device_keys.begin()->second;
                  if (device.empty()) {
                          nhlog::net()->warn("no keys retrieved from user, device {}", user_id);
                          return;
                  }

                  const auto device_keys = device.begin()->second.keys;
                  const auto curveKey    = "curve25519:" + device_id;
                  const auto edKey       = "ed25519:" + device_id;

                  if ((device_keys.find(curveKey) == device_keys.end()) ||
                      (device_keys.find(edKey) == device_keys.end())) {
                          nhlog::net()->info("ignoring malformed keys for device {}", device_id);
                          return;
                  }

                  DevicePublicKeys pks;
                  pks.ed25519    = device_keys.at(edKey);
                  pks.curve25519 = device_keys.at(curveKey);

                  try {
                          if (!mtx::crypto::verify_identity_signature(json(device.begin()->second),
                                                                      DeviceId(device_id),
                                                                      UserId(user_id))) {
                                  nhlog::crypto()->warn("failed to verify identity keys: {}",
                                                        json(device).dump(2));
                                  return;
                          }
                  } catch (const json::exception &e) {
                          nhlog::crypto()->warn("failed to parse device key json: {}", e.what());
                          return;
                  } catch (const mtx::crypto::olm_exception &e) {
                          nhlog::crypto()->warn("failed to verify device key json: {}", e.what());
                          return;
                  }

                  auto room_key = olm::client()
                                    ->create_room_key_event(UserId(user_id), pks.ed25519, payload)
                                    .dump();

                  http::v2::client()->claim_keys(
                    user_id,
                    {device_id},
                    [room_key, user_id, device_id, pks](const mtx::responses::ClaimKeys &keyres,
                                                        mtx::http::RequestErr rqerr) {
                            if (rqerr) {
                                    nhlog::net()->warn("claim keys error: {} {} {}",
                                                       rqerr->matrix_error.error,
                                                       rqerr->parse_error,
                                                       static_cast<int>(rqerr->status_code));
                                    return;
                            }

                            nhlog::net()->info("claimed keys for {}", user_id);

                            if (keyres.one_time_keys.size() == 0) {
                                    nhlog::net()->info("no one-time keys found for user_id: {}",
                                                       user_id);
                                    return;
                            }

                            if (keyres.one_time_keys.find(user_id) == keyres.one_time_keys.end()) {
                                    nhlog::net()->info("no one-time keys found for user_id: {}",
                                                       user_id);
                                    return;
                            }

                            auto retrieved_devices = keyres.one_time_keys.at(user_id);
                            if (retrieved_devices.empty()) {
                                    nhlog::net()->info("claiming keys for {}: no retrieved devices",
                                                       device_id);
                                    return;
                            }

                            json body;
                            body["messages"][user_id] = json::object();

                            auto peerdevice = retrieved_devices.begin()->second;
                            nhlog::net()->info("{} : \n {}", device_id, peerdevice.dump(2));

                            json device_msg;

                            try {
                                    auto olm_session = olm::client()->create_outbound_session(
                                      pks.curve25519, peerdevice.begin()->at("key"));

                                    device_msg = olm::client()->create_olm_encrypted_content(
                                      olm_session.get(), room_key, pks.curve25519);

                                    cache::client()->saveOlmSession(pks.curve25519,
                                                                    std::move(olm_session));
                            } catch (const json::exception &e) {
                                    nhlog::crypto()->warn("creating outbound session: {}",
                                                          e.what());
                                    return;
                            } catch (const mtx::crypto::olm_exception &e) {
                                    nhlog::crypto()->warn("creating outbound session: {}",
                                                          e.what());
                                    return;
                            }

                            body["messages"][user_id][device_id] = device_msg;

                            nhlog::net()->info(
                              "sending m.room_key event to {}:{}", user_id, device_id);
                            http::v2::client()->send_to_device(
                              "m.room.encrypted", body, [user_id](mtx::http::RequestErr cryptrqerr) {
                                      if (cryptrqerr) {
                                              nhlog::net()->warn("failed to send "
                                                                 "send_to_device "
                                                                 "message: {}",
                                                                 cryptrqerr->matrix_error.error);
                                      }

                                      nhlog::net()->info("m.room_key send to {}", user_id);
                              });
                    });
          });
}

} // namespace olm
