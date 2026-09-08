# lost-phone — finding the phone

Making the phone ring at full volume even when silenced, and having it answer where
it is. All self-hosted, on no one's account and without depending on the internet.

This is **phase 1**: the SMS channel, which is the only one that works with no data
and with no server set up. The full plan, with the in-house relay and the web
panel, is in `surya/docs/LOST-PHONE-00-plan.md`.

## How it is used

From **any phone**, an SMS to this phone:

    <clave> sonar     starts beeping, loud, even if it is silenced
    <clave> donde     answers with the position
    <clave> estado    says what it has on

**Silencing it cannot be done by SMS**, on purpose: whoever has the phone in hand
reads the key on the screen of the message that just arrived, and silencing the
alarm is exactly what they would use it for. It is silenced by unlocking the phone,
or from the panel. The cost is real and accepted: a phone ringing in a cinema needs
the PIN or the panel, not just any phone.

The key is set in *Preferences → Find my phone*, or in the app of the same name.
**With no key the channel is dead**, even if everything else is on: it is the first
check made.

## Finding it at home, with nothing set up

The case that really happens is not the mugging: it is the sofa. For a device eight
metres away and on your same wifi, setting up a server on the internet is a lot of
machinery.

```
setup/buscar-mi-movil --emparejar   once, it brings over the token via the cable
setup/buscar-mi-movil sonar         and that's it
setup/buscar-mi-movil estado
```

The phone advertises itself over mDNS as `_lost-phone._tcp` and the command finds
it. No server, no domain, and it keeps working when your internet does not.

**The advertisement is dynamic**, via the Avahi API, and not a file in
`/etc/avahi/services`: a static one would advertise this phone as locatable even
with the function off, which is a lie and a beacon at once. It also carries no TXT
record — whatever you put in there is broadcast in the clear to the whole network
non-stop.

**The token is not the SMS key.** Being on the same wifi proves nothing —guests,
the neighbour who knows the password, whatever is plugged into the router— and
reusing the key would mean a stranger on your network reads it off the phone and
then uses it over the carrier's network.

## If you already self-host an ntfy

For those who already have one and do not want to bring up another server. The
phone keeps open a `GET /<tema>/json` —the same shape as the relay, an outbound
request that is held open, and that is why it crosses CGNAT the same— and
**answers by publishing to a second topic**. That is the point: the position
reaches you as a push notification on the phone you carry, not as a page you have
to remember to open.

```
orders topic      the phone listens here:   «<clave> sonar»
replies topic     you subscribe from the ntfy app
```

Three decisions that are not cosmetic:

- **`?since=now`, never `since=all`.** Replaying history on reconnect is how a phone
  ends up ringing at three in the morning over something you sent last week.
- **Silence watchdog at 150 s.** ntfy sends a keepalive every 45; a stream quiet for
  more than three of those is dead. A channel that *looks* connected and is not is
  the worst possible failure here.
- **The topic is not a password, so there is a key as well.** ntfy's documentation
  says to treat topics as secrets: fine for "the backup finished" and not fine for
  "make my phone scream". You need both.

## If you already have a VPN (Tailscale, WireGuard)

**Nothing is needed.** The home channel is HTTP on a port, so over the VPN it works
just the same:

```
setup/buscar-mi-movil --en <ip-del-movil-en-la-vpn> sonar
```

Writing a "VPN channel" would have been inventing work.

## Why there is no MQTT

Qt does not ship an MQTT client in the base package (`QtMqtt` is a separate module,
not packaged here), so you would have to write a whole one to cover exactly what
ntfy already covers over plain HTTP. If it is ever needed, it is an isolated module
and nothing else changes.

## The four channels, and when each one serves

They are not alternatives to the same thing. Each one works where the others fail,
and you can have them all at once. **They all come off**; they are turned on one by
one.

| channel | works when | what you have to set up |
|---|---|---|
| **SMS** | whenever there is coverage, **even with no data** | nothing |
| **Your home network** | the phone is on your wifi | nothing |
| **Your relay** | the phone is **out on the street** | a server of yours |
| **ntfy** | if you already have one | nothing new |

### SMS

```
<clave> sonar      starts beeping even if it is silenced
<clave> donde      answers with the position
<clave> estado     says what it has on
```

From any phone, to the phone's number. **Silencing it cannot be done by SMS**, on
purpose: whoever has the phone in hand reads the key on the screen of the message
that just arrived, and silencing the alarm is exactly what they would use it for.
It is silenced by unlocking the phone, or from the panel.

### Your home network

```
setup/buscar-mi-movil --emparejar
setup/buscar-mi-movil sonar
setup/buscar-mi-movil --en <ip> sonar     if you are not on the same subnet
```

### The firewall, and why this seemed to work and did not

postmarketOS ships `nftables` with **`policy drop`** on the input and an
`iifname "usb*" accept` rule. That is: **over the cable everything passes, and over
wifi only what is allowed**.

The home channel was tested over the cable —which was the comfortable way, because
it was already plugged in— and taken as verified. Over wifi it never worked. And
the symptom was the worst possible: `drop`, not `reject`, so the packets are
discarded **without answering** and the browser sits there thinking until it gives
up. A "connection refused" would have led to the firewall in a minute; the silence
leads nowhere.

The setting installs `/etc/nftables.d/50_lost-phone.nft`, which opens 8479 **only
on `wlan*`** — on the home network is where it makes sense, and it is not opened on
the modem.

The lesson: **testing by the comfortable path is not testing.**

### The simple gate

A link, and that's it:

```
http://<movil>:8479/sonar?clave=TUCLAVE
http://<movil>:8479/parar?clave=TUCLAVE
```

**Those two and no others.** No `donde`, no `bloquear`, no `estado`: they answer 403
even if the home channel has those permissions. The reason is a few lines further
down -- this key travels in the address.

It exists because the token with an `Authorization` header serves a program and not
a person: it cannot be pasted into the browser's bar, put on a home-automation
button, or called from a watch. It answers in plain text, which is what someone
looking at a screen reads.

**It comes off, and has its own key.** The price of it being this easy is that the
key ends up in the browser history and in the logs of everything that sees it; that
is why it does not share a key with any other path, that is why it does not come on,
and that is why it **can only ring and silence**.

That last restriction was added on 2026-09-06, when the locate permission was
granted to the home channel: the simple gate shares a channel with the token, so it
would have inherited the permission without anyone deciding it. A key that ends up
written in the browser history, in the router log and in a home-automation script
cannot be the one that opens your coordinates.

Otherwise it broadens nothing: it goes through the same capability matrix, so it
only changes **which way** the request comes in.

### Your relay

In the panel: **Generate code → four numbers → type them into the phone**, under
*Your server*. It is the only path that works with the phone out on the street,
because the phone calls outward and so crosses CGNAT.

### ntfy

The phone listens on one topic and answers on another: the position arrives as a
push notification on the phone you carry.

## Configuring it without touching the phone

`setup/configurar-busqueda` asks path by path from the computer, generates the keys
and pairs with the relay — the one thing the phone cannot do on its own. It has
`--estado` and `--apagar`.

It took four faults to make it reliable, and the four were the same symptom:
**carrying on without having read the answer**. Always writing to `/dev/tty` and
failing with no terminal; using `{ : > /dev/tty; }`, where a redirection failure on
a *special* builtin kills the whole shell; calling the ask function inside `$( )`,
where `exit` only kills the subshell; and `ssh` without `-n`, which **eats standard
input** and left the second question with nothing to read.

None of them showed when typing by hand into a terminal. The lesson: **an
interactive interface that is not tested non-interactively is untested.**

## What to use depending on where you lost it

| where | what you do |
|---|---|
| at home | `buscar-mi-movil sonar`, or `<clave> sonar` |
| out, with data | the panel: *Ring* or *Where is it?* |
| out, no data | SMS |
| it has been taken from you | the panel: *Lock*, with a message for whoever finds it |

## What it cannot do

- **There is no remote wipe**, on purpose.
- **If it is off, no path works**: only the last position remains.
- **Each channel has its own credential** and they lend nothing to each other.
- **During the first minute after the session starts it obeys no one.** That is what
  keeps this service from taking down the boot; orders that arrive in that minute are
  picked up when it finishes.

## What you have to know before turning it on

**The key travels in the clear** over the carrier's network, and works **from any
number**. It is a decision, not an oversight: the phone you borrow from a stranger
to find yours is not going to be on any allow-list. The consequence is that whoever
figures out the key can make your phone ring, and that is why the capabilities that
can do harm come **off from the factory**:

| capability | from the factory | why |
|---|---|---|
| ring | on | the worst that can happen is noise |
| say where it is | **off** | it tells a stranger where you are |
| lock | **off** | a leaked key would lock you out |

## Lost mode

A lock order leaves the phone in **lost mode**, which is not a variable but a state
written to disk (`~/.local/state/lost-phone/modo-perdido`) and reread on every boot
— because the first thing whoever picks up someone else's phone does is reboot it.
While it lasts:

- it always locks, and **`smart-unlock` stops granting trust**, not even on the
  network you marked. With the stolen phone inside your house, that network and the
  thief are the same place;
- the message you set appears on the lock screen, as a resident notification —
  without patching any system file, because that screen already draws notifications
  out of the box;
- **you only leave lost mode with the PIN or from the panel.** Whoever can lock
  cannot unlock: that is the difference between a safety net and a toy.

What does **not** survive, stated plainly: a reflash, and a thief who already knows
your PIN. It is made for "lost + casual theft", which is the threat that was chosen,
and it does not pretend to be more.

Also, one order per minute at most, so no one turns the phone into a perpetual
siren.

## What it ships

    kcm_lost_phone.so     the module inside Preferences
    lost-phone            the app, in the drawer
    lost-phoned           the daemon that listens and obeys
    lost-phoned.service   the user unit that keeps it alive

The module and the app are **two frames over the same backend**. A KCM depends on
the KF6 ABI and an update can leave it unable to load; the settings of something you
only touch on the worst day are exactly the ones that cannot become unreachable.

**The daemon is what makes the function exist.** The interface only writes a file;
without `lost-phoned` running, no one reads that file and no SMS does anything. That
is why the screen warns in red when the service is stopped.

    systemctl --user status lost-phoned.service

## Why it runs as user and not as root

Measured on the surya: `mmcli --messaging-list-sms` and `mmcli --location-get` work
as a normal user, and **PulseAudio, which owns the sound card, only exists inside
the session**. A root daemon would have to reach into the session for exactly what
it most needs to do.

What does need to survive the session is *lost mode*, and that arrives in phase 4
with its own root helper. Nothing here pretends to survive a session logout, because
it does not.

## How it locates

They are tried in order and the best one that answers wins, **always with its
accuracy and its time**. A position with no accuracy is a lie with coordinates.

| channel | accuracy | where it comes from |
|---|---|---|
| GPS | 5-20 m | `mmcli --location-get`, with `gps-raw` already enabled on this phone |
| wifi | says whether it is at home | `nmcli`: the strongest network it sees |
| cell | 0.5-3 km | the same `mmcli`: MCC/MNC/LAC/CID |
| last known | whatever it is | saved on each good fix |

Measured indoors on 2026-09-04, and it is exactly the argument for the cascade:

    GPS   $GPGSA,A,1,...    eleven satellites in view, NO fix
    3GPP  mcc 214 mnc 03 lac 3AFE cell id 07F17CB5   <- this one did answer

A "find my phone" that only asks the GPS is useless precisely in the place where
people lose the phone.

The translation of BSSID to coordinates **is not done by the phone**: it needs a
database, and that lookup belongs to the phase 3 relay, so the phone never talks to
a third party.

## The screen that appears when it rings

It is not a notification: it is a **full screen** with a round **STOP** button in
the centre, the size of a hand. A notification is something you have to find; this
is already in front of you.

It cannot appear **over** the lock screen —nothing can, that is what a lock is
for— so with the phone locked you unlock and there it is, waiting. That is the PIN
working as requested, not a limitation to hide.

It closes by itself when the alarm stops for any reason: the seconds were met, it
was stopped from the panel, someone sent `parar`. A screen that demands you stop
something already stopped is a screen that teaches you to ignore it.

And it is launched **in its own cgroup** (`systemd-run --user --scope`). It is not a
detail: `startDetached` left it inside the daemon's cgroup, which has `MemoryMax`,
and a whole QtQuick app inside a limit meant for a daemon that never draws ended
with the OOM killer killing **the whole group** two seconds after it started
ringing. Measured: one second of Blip and silence.

## The tune and the volume

**Blip** by default, one second, from the sound theme the phone already ships. It is
changed in the settings among the 23 installed tunes and the generated strident
tone. The volume goes from 10 % to 100 %.

Blip and not a pretty tune, on purpose: **the ear places a sound by its onsets**. A
one-second clip on a loop gives sixty onsets a minute; a forty-second ringtone gives
one and a half. That is why the generated strident tone is still available instead
of having been replaced: it is even uglier and even better by that measure.

If the chosen tune disappears —the theme uninstalled, a path that no longer exists—
the generated tone plays and it says so in the journal. A tune that vanished cannot
turn into a mute phone right when you are looking for it.

## How it sounds

Measured with the phone silenced to zero, which is the real state of a lost phone:

    pcm0p/sub0/status -> state: RUNNING
    i2c bus9 0x4c/0x4d PWR_CTL=0x0c    both tas2562, ACTIVE

And the limit the design imposes: **PulseAudio owns the card**, and `aplay -D
hw:0,0` while it is alive answers `Resource busy`. So the path is through Pulse when
Pulse is there, and ALSA directly only when it is not.

If the daemon dies suddenly -- `SIGKILL`, out of memory, the session leaving
mid-alarm -- the volume is restored by `lost-phoned --restaurar`, hooked to the
unit's `ExecStopPost`. Without that backup, a phone could stay at full volume
forever.

The volume **is always restored** on finishing. It is not courtesy: the `audio`
setting establishes that the volume has a single writer and is persisted with
`alsactl store`. A `lost-phone` that left the phone at full volume forever would
break that silently.

## The order's SMS is deleted

On this phone there are **two consumers of the same SMS** — `spacebar-daemon` and
`kde-telephony-daemon` — so the order would appear in the messages app. It is
deleted as soon as it acts, and before locating, because locating takes up to a
minute and a half and an order sitting there is an order someone can read over your
shoulder.

## A trap that cost hours: the translated output

`pactl get-sink-mute` prints `Mute: yes` with `LC_ALL=C` and **`Mute: sí`** inside a
Spanish session. The code looked for `"yes"`, so it recorded *"not muted"*
**always**, and after each alarm returned the phone unmuted. The volume did come
back fine, because there a number with `%` is searched for and numbers are not
translated — and that half that worked is what made the symptom so hard to read.

Worse: **all the tests over SSH passed**. An SSH session carries no language, so
whoever tested saw English and the daemon saw Spanish. A different environment from
the one that failed was being measured.

That is why there is a single `src/proceso.h`: **nothing in this package launches a
program whose output is read any other way**, and that function sets `LC_ALL=C`.
`mmcli` and `nmcli`, where the cell and the wifi networks come from, have the same
trap waiting.

## The position refreshes itself

The heartbeats say the phone **is still alive**; they do not say **where it is**.
They are two different things, and for a while the panel showed "connected a minute
ago" over a map from an hour ago -- the kind of datum that looks useful and is not.

Now the phone refreshes its position on its own: every **30 minutes**, every **5 in
lost mode**, and once **at boot**, so that whoever just rebooted the phone does not
see yesterday's map.

That background refresh **does not turn on the radios**, and that is deliberate: it
happens every half hour and forever, and leaving the GPS on in perpetuity for a
refresh would be trading the phone's battery for a convenience. Whoever is SEARCHING
for the phone does turn everything on -- and if a real search comes in while a
refresh is running, it drops it and starts from scratch, so as not to answer without
wifi or GPS without saying so.

## HTTP/1.1 with the relay, and not HTTP/2

Qt negotiates HTTP/2 if the server offers it, and then it **multiplexes**: all the
requests travel over one connection. One of them is the long wait, held open for a
minute. When the server closes that connection it takes with it whatever was inside
-- and what was inside was the sending of the position, which died in silence
leaving only a `qt.network.http2: Received GOAWAY` in the journal.

With HTTP/1.1 each request carries its own and the long wait drags no one along.

## Powering off the phone from the panel

It exists, and **it comes off**. It is the only order that switches off all the
others: a powered-off phone does not ring, does not say where it is and obeys no
one, not even you. It can only be done over the relay channel -- which is behind a
password -- and the panel button forces you to type `APAGAR`. Over SMS and over the
home network it is denied in the matrix, with no switch to open it: over SMS the key
travels in the clear, and on the home network being on the wifi is enough.

## Tests

    test/ejecutar

Compiles and runs inside pmbootstrap's aarch64 chroot, because on the laptop there
is no usable Qt6. Five suites, 90 checks. They cover the capability matrix (which
**is** the security model) and the NMEA conversion, which is where a fault sends you
looking for the phone nineteen kilometres to the south.

## Where it is edited

In `surya/setup/ajustes/lost-phone.d/`. The copy in
`surya/pmaports/temp/lost-phone/` is disposable and is written by
`surya/pmaports/sincronizar lost-phone`.
