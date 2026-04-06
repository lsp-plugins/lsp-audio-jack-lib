# lsp-audio-jack-lib

Audio backend for JACK

Requirements
======

The following packages need to be installed for building:

* gcc >= 4.9
* make >= 4.0

Building
======

To build the library, perform the following commands:

```bash
make config # Configure the build
make fetch # Fetch dependencies from Git repository
make
sudo make install
```

To get more build options, run:

```bash
make help
```

To uninstall library, simply issue:

```bash
make uninstall
```

To clean all binary files, run:

```bash
make clean
```

To clean the whole project tree including configuration files, run:

```bash
make prune
```
