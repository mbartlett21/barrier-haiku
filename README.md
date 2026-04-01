# Barrier for Haiku

![Barrier for Haiku](BarrierHaiku.png "Barrier for Haiku")

Barrier for Haiku is a [Barrier](https://github.com/debauchee/barrier) client for the [Haiku Operating System](http://haiku-os.org).

It also supports [Deskflow](https://github.com/deskflow/deskflow) and should also work with [Synergy](https://symless.com/synergy)

Barrier allows a central machine running the Barrier server to share its Keyboard and Mouse across multiple systems running the client as if they were one desktop.

## Limitations

  - Barrier for Haiku is only a Barrier client, not a server
  - Some minor bugs still exist in the keymap translation
  - SSL / TLS configuration needs to set up in the configuration file and will not be automatically detected
  - Requiring client certificates is not currently supported and must be disabled on the server
  - Clipboard support only supports plaintext both ways

## Compiling

Simply run `make ; make install` under Haiku.

For updating, you *must* do a `make uninstall` before `make install`, otherwise you will visit the kernel debugger.

## Configuration

Create a configuration file at `~/config/settings/barrier`

  ```ini
  enable = true
  server = 192.168.1.101
  server_keymap = "X11"
  server_ssl = true
  server_fingerprint = "v2:sha256:somebiglonghashstringthatissixtyfourcharacterslongjustlikethisis"
  ```

### Options

  * **enable**: Enable the client (true|false)
  * **server**: Server address
  * **server_keymap**: Keymap of the Barrier Server (X11|AT). `AT` for Windows servers.
  * **server_ssl**: Whether the server uses ssl/tls (true|false). If server_fingerprint is specified, this is forced on.
  * **server_fingerprint**: The server fingerprint. This can be found under `tls/local-fingerprint` on the server, or in the client's syslog once it has connected.
  * **client_name**: Name of client (string, "haiku" default)

## Manual Installation

Copy the generated `barrier_client` input add-on to the non-packaged add-ons directory: `~/config/non-packaged/add-ons/input_server/devices/`

## License

Barrier for Haiku is released under the same license as uBarrier (MIT)

## Thanks

Thanks to all of those individuals who have made major contributions to Barrier for Haiku.

* Axel Dörfler (ATKeymap)
* Stefano Ceccherini (wrapper)
* Jérôme Duval (Keymap)
* Alex Evans (uBarrier)
* Jessica Hamilton (wrapper)
* Ed Robbins (wrapper)
