<!-- SPDX-License-Identifier: MIT -->

# watch

A small Linux utility that reruns a command at a regular interval, clearing the terminal before each run.

## Build

Requires `make` and a C11 compiler on Linux. It was built and tested in WSL with Ubuntu. Run:

```sh
make
```

## Usage

```sh
./watch [-n SECS | --interval SECS] COMMAND [ARGS...]
./watch --help
```

The interval defaults to 2 seconds. For example:

```sh
./watch -n 5 date
```

Runs do not overlap. If a command takes longer than the interval, the next run starts immediately. Press Ctrl+C to stop.

## Credits

Developed by Erko Puskar. AI was used mainly to help debug the program and fix errors.

## License

MIT. See the repository's root license file.
