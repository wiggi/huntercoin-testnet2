Huntercoin testnet2
===================

Human-mineable crypto currency / decentralized game

Testnet2 is a complete overhaul of Huntercoin as a simulation game with strategy and RPG elements (see Gamerules_testnet2.txt)

Testnet2 doesn't interfere with an existing installation of Huntercoin, always starts on testnet, using 'testnet2' subfolder

Windows build: (use 'huntercoin-qt.exe' to play online, or the shortcuts for offline testing)

hunttest_20170715.zip, 18.1 MB

https://mega.nz/#!SR8lCKRY!37mizyFDEjL8tY2J2G_SXhLslbX0X8zX0Q6sfpjxOtY

To build on a new Ubuntu 16.04 or Linux Mint 18

    sudo apt-get install libboost-chrono-dev libboost-date-time-dev libboost-filesystem-dev libboost-program-options-dev libboost-serialization-dev libboost-system-dev libboost-thread-dev
    sudo apt-get install libboost-dev git qt4-qmake libqt4-dev build-essential qt4-linguist-tools libssl-dev
    sudo apt-get install libdb++-dev

if Qt Creator is installed after this, open huntercoin-qt.pro, and Build | Build project huntercoin-qt, otherwise

    qmake
    make

