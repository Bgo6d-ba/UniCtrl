Welcome to the Deskflow Help!

## Introduction
* [Homepage](https://deskflow.org) - Project home page
* [Code](https://github.com/deskflow/deskflow) - Read the code

## Host and join

Share one computer's keyboard and mouse with the other computers on your network, without typing addresses:

1. On the computer whose keyboard and mouse you want to share, choose **Host**. It shows a six-digit code,
   for example `482 915`.
2. On each other computer, choose **Join**, pick the host under **Nearby hosts** and enter the code.
3. On the host, choose **Arrange screens…** to place the computers that joined next to each other.

The code is checked on both computers without ever being sent, which also makes sure that each computer
talks to the right one. A computer that joined before shows the host as **Paired** and joins again without
a code. If a computer can't connect to a host it joined before within 20 seconds, Deskflow forgets that the
host was paired and asks for the code again.

* **Your name** at the top is what the other computers show for this one.
* **Stop hosting** or **Leave** disconnects.

### The code

* The code stays valid until you choose **Stop hosting** or **New code**, or until it is replaced after too
  many wrong codes (see below). It is not a one-time code: everyone who enters it while it is shown can join,
  so choose **New code** when the computers you wanted have joined.
* At most 16 computers can join during one hosting. After that nobody can join until you choose **New code**.
* **Change password…** makes the host ask for a password of your own instead of a code. It needs at least eight
  characters, typed twice. It is stored as plain text in the `password` entry of the `[lan]` section of the
  settings file, so anyone who can read that file can read it. **New code** goes back to a random code and removes
  the stored password.
* Wrong codes are counted. A computer that enters 5 wrong codes within a minute is refused for a minute. When 15
  wrong codes come from all computers together within a minute, nobody can join for a minute, and a code that
  Deskflow made is replaced by a new one, which the host is told about. A password of your own is never replaced;
  only the pause protects it, so choose a long one.

### Paired computers

* **Paired** next to a host means that this computer joined that host with a code before. It is an entry saved on
  this computer, made from the id the host announces, and it says that no code is needed. It does not check who
  is on the network now: every connection is still checked against the certificate the host had when this
  computer joined it, and the entry is removed when that does not match.
* Joining trusts the other computer's certificate for good, on both sides. There is no way to take it back in the
  app yet. To remove a computer, quit Deskflow and delete its line from `tls/trusted-clients` (on the host, for a
  computer that joined it) or from `tls/trusted-servers` (on the computer that joined, for a host), in the folder
  of the settings file. The line holds the fingerprint that the other computer shows with the fingerprint button in
  its status bar. Delete the host's entry under `pairedHosts` as well, if this computer joined it.
* If a computer gets a new certificate (a new install, or you regenerate it in Preferences), the computers that
  joined it before have to join again. Deskflow tells you when it had to create a new certificate to host or join.

### Network

* All computers must be on the same local network, on the same IPv4 subnet, for example the same Wi-Fi. Hosts on
  another subnet, behind a router or reached over a VPN are not listed, and computers there can't join.
* Deskflow finds hosts on UDP port 24802 and joins on TCP port 24803, and then connects on TCP port 24800 as before.
  The hosts are only looked for while **Nearby hosts** is open.
* Windows may ask whether to allow Deskflow through its firewall; allow it.
* macOS asks to find devices on your local network the first time. If you said no, open **System Settings**,
  **Privacy & Security**, **Local Network** and turn on Deskflow.
* Linux never asks. Open the ports on the host and on the computers that join, for example
  `sudo ufw allow 24800/tcp && sudo ufw allow 24803/tcp && sudo ufw allow 24802/udp` for ufw, or
  `sudo firewall-cmd --permanent --add-port=24800/tcp --add-port=24803/tcp --add-port=24802/udp && sudo firewall-cmd --reload`
  for firewalld.

### Simple and Advanced

* **Simple** is the view you start with. Nothing starts, listens or changes until you choose **Host** or **Join**.
* Hosting always uses encryption and checks the certificates of the computers that connect. If the server had these
  turned off, choosing **Host** turns them on and restarts the server. Only computers whose certificate is trusted
  can connect: those that joined with the code, and any you trusted earlier under **Advanced**. While hosting or
  joining, Deskflow does not ask whether to trust a computer, so a computer it does not know is refused; join it
  with the code.
* If Deskflow starts again with the server running (it starts the server with the application) and you were
  hosting when it closed, the server shows as hosting again, with a new code. Nothing is turned on or restarted for
  that: it only shows as hosting when encryption and certificate checks are already on. A server or client you set
  up under **Advanced** is left as it is; the Host and Join page says that a manual session is running and has a
  button to **Advanced**. A client that runs is shown as connected.
* **Advanced** shows the server and client settings of earlier versions; **Simple** goes back. Switching does not
  stop anything.

## User Guides
* [Configuration] - Configuration files
* [Raising Issues] - What to do if you have found an issue.
* [Security Policy] - Our policy around security related topics

## Getting help online

* View the [Wiki](https://github.com/deskflow/deskflow/wiki) Online resource

### Chat with us
* Main discussion on Matrix: [`#deskflow:matrix.org`](https://matrix.to/#/#deskflow:matrix.org) ([Matrix clients](https://matrix.org/ecosystem/clients/))
* Discussion also happens on IRC: `#deskflow` or `#deskflow-dev` on [Libera Chat](https://libera.chat/)
* Start a [new discussion](https://github.com/deskflow/deskflow/discussions) on our GitHub project.

[Configuration]:Configuration.md
[Raising Issues]:Issues.md
[Security Policy]:Security.md

