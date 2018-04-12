nheko
----
[![Build Status](https://travis-ci.org/mujx/nheko.svg?branch=master)](https://travis-ci.org/mujx/nheko)
[![Build status](https://ci.appveyor.com/api/projects/status/07qrqbfylsg4hw2h/branch/master?svg=true)](https://ci.appveyor.com/project/mujx/nheko/branch/master)
[![Latest Release](https://img.shields.io/github/release/mujx/nheko.svg)](https://github.com/mujx/nheko/releases)
[![Chat on Matrix](https://img.shields.io/badge/chat-on%20matrix-blue.svg)](https://matrix.to/#/#nheko:matrix.org)
[![AUR: nheko-git](https://img.shields.io/badge/AUR-nheko--git-blue.svg)](https://aur.archlinux.org/packages/nheko-git)
[![AUR: nheko](https://img.shields.io/badge/AUR-nheko-blue.svg)](https://aur.archlinux.org/packages/nheko)

The motivation behind the project is to provide a native desktop app for [Matrix] that
feels more like a mainstream chat app ([Riot], Telegram etc) and less like an IRC client.

### Features

Most of the features you would expect from a chat application are missing right now
but we are getting close to a more feature complete client.
Specifically there is support for:
- User registration.
- Creating, joining & leaving rooms.
- Sending & receiving invites.
- Sending & receiving files and emoji (inline widgets for images, audio and file messages).
- Typing notifications.
- Username auto-completion.
- Message & mention notifications.
- Redacting messages.
- Read receipts.
- Basic communities support.
- Room switcher (ctrl-K).
- Light, Dark & System themes.

### Installation

There are continuous nightly releases [here](https://github.com/mujx/nheko/releases/tag/nightly)
for Linux ([AppImage](https://appimage.org/), rpm, deb), Mac and Windows.

#### Arch Linux
```bash
pacaur -S nheko-git
```

#### Fedora
```bash
sudo dnf install nheko
```

#### Gentoo Linux
```bash
sudo layman -a matrix
sudo emerge -a nheko
```

#### Alpine Linux (and postmarketOS)

Make sure you have the testing repositories from `edge` enabled. Note that this is not needed on postmarketOS.

```sh
sudo apk add nheko
```

### Build Requirements

- Qt5 (5.7 or greater). Qt 5.7 adds support for color font rendering with
  Freetype, which is essential to properly support emoji.
- CMake 3.1 or greater.
- [LMDB](https://symas.com/lightning-memory-mapped-database/).
- A compiler that supports C++11.
    - Clang 3.8 (or greater).
    - GCC 7 (or greater).
- [json.hpp](https://github.com/nlohmann/json/blob/develop/single_include/nlohmann/json.hpp) from [Niels Lohmann's JSON library](https://github.com/nlohmann/).

#### Linux 

If you don't want to install any external dependencies, you can generate an AppImage locally using docker.

```bash
make docker-app-image
```

If you're on Debian you should use `make docker-debian-appimage` instead, which uses
Debian as the build host in an attempt to work around this [issue](https://github.com/AppImage/AppImageKit/wiki/Desktop-Linux-Platform-Issues#openssl).

##### Arch Linux

```bash
sudo pacman -S qt5-base qt5-tools qt5-multimedia cmake gcc fontconfig lmdb
```

##### Gentoo Linux

```bash
sudo emerge -a ">=dev-qt/qtgui-5.7.1" media-libs/fontconfig
```

##### Ubuntu (e.g 14.04)

```bash
sudo add-apt-repository ppa:beineri/opt-qt592-trusty
sudo add-apt-repository ppa:george-edison55/cmake-3.x
sudo apt-get update
sudo apt-get install -y qt59base qt59tools qt59multimedia cmake liblmdb-dev
```

To build on Ubuntu 14.04 Trusty out-of-the-box requires using Clang 3.6 instead of GCC:

```bash
sudo apt-get install clang-3.6
export CC=clang-3.6 CXX=clang++-3.6
```

On Ubuntu 14.04 Trusty, it's possible to use GCC 4.9.4+, but it is not recommended, because it requires installing GCC packages from third-party PPAs.  Later versions of Ubuntu that come with GCC 4.9.4+ should work with GCC out-of-the-box.

##### macOS (Xcode 8 or later)

```bash
brew update
brew install qt5 lmdb cmake llvm
```

### Building

Clone the repo and run

```bash
make release
```

which invokes cmake and translates to

```bash
cmake -H. -Bbuild -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

If the build fails with the following error
```
Could not find a package configuration file provided by "Qt5Widgets" with
any of the following names:

Qt5WidgetsConfig.cmake
qt5widgets-config.cmake
```
You might need to pass `-DCMAKE_PREFIX_PATH` to cmake to point it at your qt5 install.

e.g on macOS

```
cmake -H. -Bbuild -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=$(brew --prefix qt5)
cmake --build build
```

The `nheko` binary will be located in the `build` directory.

#### Nix

Download the repo as mentioned above and run

```bash
nix-build
```

in the project folder. This will output a binary to `result/bin/nheko`.

You can also install nheko by running `nix-env -f . -i`

### Contributing

Any kind of contribution to the project is greatly appreciated. You are also
encouraged to open feature request issues.

### Screens

Here is a screen shot to get a feel for the UI, but things will probably change.

![nheko](https://dl.dropboxusercontent.com/s/a493o47obvez6v1/nheko.png)

### Third party

- [Emoji One](http://emojione.com)
- [Font Awesome](http://fontawesome.io/)
- [Open Sans](https://fonts.google.com/specimen/Open+Sans)

[Matrix]:https://matrix.org
[Riot]:https://riot.im
