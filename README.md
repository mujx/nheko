nheko
----
[![Build Status](https://travis-ci.org/mujx/nheko.svg?branch=master)](https://travis-ci.org/mujx/nheko) [![Build status](https://ci.appveyor.com/api/projects/status/07qrqbfylsg4hw2h/branch/master?svg=true)](https://ci.appveyor.com/project/mujx/nheko/branch/master)

The motivation behind the project is to provide a native desktop app for [Matrix] that
feels more like a mainstream chat app ([Riot], Telegram etc) and less like an IRC client.

Join the discussion on Matrix [#nheko:matrix.org](https://matrix.to/#/#nheko:matrix.org).
### Features

Most of the features you'd expect from a chat application are missing right now
but you can of course receive and send messages in the rooms that you are a member of.

### Requirements

- Qt5 (5.7 or greater). Qt 5.7 adds support for color font rendering with
  Freetype, which is essential to properly support emoji.
- CMake 3.1 or greater.
- A compiler that supports C++11.
    - Clang 3.3 (or greater).
    - GCC 4.8 (or greater).

##### Arch Linux

```bash
$ sudo pacman -S qt5-base cmake gcc fontconfig
```

##### Ubuntu 14.04

```bash
$ sudo add-apt-repository ppa:beineri/opt-qt58-trusty
$ sudo add-apt-repository ppa:george-edison55/cmake-3.x
$ sudo apt-get update
$ sudo apt-get install qt58base cmake
```

##### OSX (Xcode 7 or later)

```bash
$ brew update
$ brew install qt5
```

N.B. you will need to pass `-DCMAKE_PREFIX_PATH=/usr/local/Cellar/qt5/5.8.0_2`
to cmake to point it at your qt5 install (tweaking the path as needed)

### Building

```bash
cmake -H. -Bbuild -DCMAKE_BUILD_TYPE=Release # Default is Debug.
make -C build
```

The `nheko` binary will be located in the `build` directory.

[Here](https://ci.appveyor.com/project/mujx/nheko/branch/master/artifacts) you can
find development builds for Windows.

### Contributing

Any kind of contribution to the project is greatly appreciated. You are also
encouraged to open feature request issues.

### Screens

Here is a screen shot to get a feel for the UI, but things will probably change.

![nheko](https://dl.dropboxusercontent.com/s/cs2891enrf2lqo9/nheko.jpg)

### Third party

- [Emoji One](http://emojione.com)
- [Open Sans](https://fonts.google.com/specimen/Open+Sans)


### License

[GPLv3]

[Matrix]:https://matrix.org
[Riot]:https://riot.im
[GPLv3]:https://www.gnu.org/licenses/gpl-3.0.en.html
