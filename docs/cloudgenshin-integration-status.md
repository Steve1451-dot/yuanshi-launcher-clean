# CloudGenshin-NX native integration alpha (updated 2026-10-23)

## Offline input-lifecycle fixes (0.2.71)

These are code-review fixes verified by the host suite only; no live session is
claimed to be fixed by them.

- **Channel recovery resets every per-channel announcement.** The input
  channel already re-announced the virtual pad after a drop, but the
  input-source declaration, the device/screen record, and the initial cursor
  position were still gated by one-shot flags. A rebuilt channel therefore ran
  with a stale source declaration and no device record. All of them are now
  re-sent on the rising edge of `input_ready()`, and the synthetic entry holds
  and any pending pointer release are dropped (they cannot be released on the
  dead channel).
- **Synthetic A now survives the continuous sender.** The sender writes the
  physical pad state every ~16 ms, so a one-shot entry press was overwritten by
  the next neutral frame and the pod saw a single-tick edge. `ComposeGamepadFrame`
  re-applies A for the whole hold window for both the Continue request and
  automatic entry. Covered by `mihoyo_stream_input_policy`.
- **Entry release cannot be skipped.** When the game reported its enter
  transition while A was still held, the auto-entry block stopped running and
  the release was never sent, leaving A pressed for the rest of the session.
  A pending hold/tap now keeps the block alive until it is released.
- **Opt-in autotest entry follows the documented transport policy.** It used
  touch unconditionally even though its comment promised pad A when a virtual
  pad exists; it now sends the same held A on pad terminals.
- **Neutral handoff contract pinned.** A pad change that happened while the
  channel was down still gets its neutral frame on the new channel, and
  `reset()` no longer pretends it was delivered.
- **Queue abandonment is a metric, not a failure.** `QUEUE_ABANDONED_TOO_LONG`
  is recorded with the observed seat but does not fail the run.

Build evidence for this batch: version 0.2.71, NRO SHA256
`C91CE322833907A4564BFD122C7DA17E3F15C40BE8FFDE2B27D509EFA70077E9`,
host suite 51/51 PASS after the final edits. This is an offline verification
only: it does not claim that the Star Rail title page or `1001_2` dialog is
solved live.

## Declare the input source on channel open (0.2.70)

`Session::reset_game_inputs()` (the official type-31
`resetGameInputControl` frame) existed but was never called anywhere. The
official client sends it whenever its UI takes over input, and the pod uses the
declared `ClientCore.GameContorlType` bitmask to decide which device owns
input. Without any declaration the pod can keep routing to a device that never
speaks.

The view now declares the source once, as soon as the input channel opens:
`TOUCH_SCREEN` on touch-launcher terminals with a pad transport,
`GAMEPAD` on mouse-launcher terminals with a pad transport, and `MOUSE`
otherwise. It retries until the frame is actually written and logs
`INPUT_SOURCE_DECLARED` with the bitmask.

This targets the exact Star Rail symptom: pad list announced, type-17 frames
written successfully, but the title page never reacts. If declaring the source
unblocks entry, the earlier `idg`/fingerprint/pointer investigations were all
about the wrong layer.

## `idg` default-pad hint for all three titles (0.2.69)

The launch-time default-pad preference `user_data.idg=true` (the signed
equivalent of `-is_default_gamepad 1`) was only sent for `hk4e`. Star Rail and
ZZZ dispatched without it, so the pod was never asked for a default virtual
pad. Both titles now send it, and the dispatch test pins all three profiles.

This is the leading explanation for the Star Rail title-page symptom: the pad
list and type-17 frames were accepted by the client-side SCTP channel
(`GAMEPAD_CHANNEL_SEND_OK_NOT_DELIVERY_PROOF`) but nothing reached the title
page. If the pod never created a virtual pad there is nothing to dispatch the
frames to, which also explains why the synthetic pointer tap and the pad-A
press both did nothing while `RPGCloudSave` kept proving the account was
logged in.

## Gamepad-A launcher entry (0.2.68)

Frame dumps from the Star Rail runs settled the entry question. The title page
shows the real "点击进入" bar and we do send touch events (`out rtc touch` down
and up), yet the picture stays on the title screen. Both terminal 6 (touch) and
terminal 10 (mouse) behave the same, so this is not a pointer coordinate bug:
the Star Rail title page ignores the synthetic pointer tap.

Auto-entry now prefers a real XInput A press (`AUTO_ENTRY_PAD_PRESS` →
`AUTO_ENTRY_PAD_TAP_SENT`) whenever a virtual pad exists, holding A for 120 ms
and releasing with a neutral frame. Touch stays as the fallback only when no
pad transport is available. This removes the pointer-coordinate dependency
from the entry path entirely, and matches how a physical controller enters the
title page.

The `1001_2` dialog is now understood as a consequence, not the cause: it
appears after the title page has been sitting un-entered for roughly a minute
while the game's own login session ages out (the game still writes
`RPGCloudSave`, so it is logged in). Entering with A should avoid it.

## Device fingerprint probe (0.2.67, negative result)

`info_get_device_fingerprint` used to reply with an empty string; it now returns
a deterministic 13-character fingerprint and logs
`GAME_SDK_DEVICE_FINGERPRINT_PRESENT=1`. A live run confirmed the non-empty
reply but the `1001_2` dialog still appeared, so the fingerprint is NOT the
root cause. That change is kept (it matches the official reply shape) but the
entry timing is the actual fix.

## Star Rail 1001_2 root-cause work (0.2.67)

Star Rail reaches the real in-game client. Two independent terminal runs
(terminal 6 and terminal 10) produced:

- `LOGIN_RESTORED`, dispatch at 3 s, `GAMEPAD_LIST_ANNOUNCED=1118/654`,
  `LIVE_VIDEO_DECODED` at 7 s, and a real Star Rail loading frame in the dump.
- `GAME_SDK_LOGIN_CHANNEL_TOKEN_PRESENT=1`, `METADATA_PRESENT=1`,
  `COMBO_ID_PRESENT=1`, all `GAME_SDK_REPORT_RETCODE=0`.
- `cloud_set_data keys=RPGCloudSave` from +32 s every 30 s through +145 s.
  Cloud-save writes only happen inside the running game, so the account does
  log into the game; this is not a login failure.

What actually goes wrong: at +58.8 s the game queries `info_get_cps`,
`info_get_device_id` and `info_get_device_fingerprint`, and the frozen
"check network / proxy and log in again, code 1001_2" dialog appears right
after. The fingerprint reply used to be an empty string.

- `info_get_device_fingerprint` now returns a deterministic 13-character
  fingerprint derived from the persisted device id (FNV-1a 64 over the id,
  base36, padded to 13). Stable across launches, no new identity invented.
  `GAME_SDK_DEVICE_FINGERPRINT_PRESENT=1` records the presence only.
- `InspectGameReport` additionally records `GAME_SDK_REPORT_NET_1001_2=1` if a
  game report body mentions that exact token, so the next run can prove
  whether the dialog is still raised.

Still open: whether the non-empty fingerprint clears 1001_2. If it does not,
the remaining suspects are the cloud pod's own outbound network self-check and
the account's cloud entitlement for Star Rail.

## Shared miHoYo passport login, queue cap, Endfield title entry (0.2.66)

- All three miHoYo CN cloud titles share one passport account. The vault keeps
  one Combo session per game (different `app_id`/`sdk_base`), but the
  passport-login flow now accepts `hk4e`, `hkrpg` and `nap` and builds the
  Combo `webLogin` request from each profile's own `game_biz`/`app_id`/
  `sdk_base` instead of the hardcoded Genshin values. Unknown hosts are still
  rejected. A vault entry is only reused for its own game.
- Verified with the user's fresh scan: the saved entries are the same account
  (`same_account hk4e/hkrpg = YES`, `hk4e/nap = YES`, `hkrpg/nap = YES`), and
  both `hkrpg` and `nap` entries carry `channel_token` plus Combo SDK metadata.
- Star Rail end-to-end: `LOGIN_RESTORED`, normal-queue dispatch at 3 s,
  `GAMEPAD_LIST_ANNOUNCED=1118/654`, `LIVE_VIDEO_DECODED` at 7 s,
  `GAME_SDK_LOGIN_CHANNEL_TOKEN_PRESENT=1`, `METADATA_PRESENT=1`,
  `COMBO_ID_PRESENT=1`, auto-entry 3 taps then `AUTO_ENTRY_SCENE_CHANGED`.
  4295 decoded frames, 0 decode errors, 7600 audio packets.
- ZZZ: login and dispatch succeed; the first attempt sat in the normal queue
  around 270-284 for the whole 554 s window. That is queue size, not a code
  failure, so ZZZ is left for a quieter time.
- Normal queue is now abandoned automatically above 100 waiting players
  (`QUEUE_ABANDONED_TOO_LONG`), for both the miHoYo and Endfield flows. It
  leaves the queue and frees the allocation; it never switches to the paid
  queue.
- Endfield: v1 vault without a uid now upgrades itself through the official
  binding API (`ENDFIELD_STREAM_UID_RECOVER` → `_RECOVERED`) instead of
  demanding a re-scan; failures fall back to the Skland QR. Its stream view
  gained a title-page click (mouse left at centre, once a frame is decoded,
  bounded to 12 tries, stops at `game_started`), a 120 s first-frame wait, and
  the same handheld/paired controller source policy (no cross-pad merging,
  12% radial deadzone). Skland QR generation was observed working
  (`ENDFIELD_QR_WAITING`); that run expired unscanned after 181 s.
- Host tests: 51 PASS / 0 FAIL after the passport-profile widening and the
  queue-cap monitor rule.

## Low-bitrate request, session-end handling, quality persistence (0.2.64)

- Dispatch `bit_rate` is now the official low-tier ceiling `kStartupBitRate =
  4000000` bit/s (4000 kbps). The previous hardcoded `2560000` under-requested
  the low tier, so the pod could start below the documented 4 Mbps floor.
  `DISPATCH_BIT_RATE` records the exact signed request value.
- Server command 20005 `StopGameRsp` is now decoded on both the signaling proxy
  and the RTC/web-control data channel. It is no longer treated as an unknown
  message followed by a generic `SIGNALING_DISCONNECTED`: the session is marked
  ended (`GAME_SDK_SESSION_ENDED`), `stop()` does not echo a StopGameReq back,
  and the diagnostics panel offers an explicit `Reconnect` button. That button
  reuses the saved login and the normal queue; login, QR, queue and logout are
  never auto-clicked.
- Quality/FPS choices already persisted atomically to
  `cloudgenshin-stream-settings.txt` with a `.bak` fallback. The startup log now
  reports the value actually requested (`STREAM_STARTUP_QUALITY`) and whether it
  came from a saved file (`STREAM_SETTINGS_RESTORED`); a write failure emits
  `STREAM_SETTINGS_SAVE_FAILED` instead of silently looking successful.
- The first-frame wait is 120 s in this build. Terminal 6 decoded its first frame
  at 86 s on the same service, so the old 60 s limit aborted before entry.
- Terminal-6 live run `0.2.64-term6-full-log-1` (182 s, NRO SHA256
  `05451168416966E26F13C06FA9E862C8A7EF6D8974D1B0160A227FB1F2989005`)
  produced: `DISPATCH_BIT_RATE=4000000`, `STREAM_SETTINGS_RESTORED=1`,
  `LIVE_VIDEO_DECODED` at 15 s, `GAMEPAD_ONLY_MODE_ACTIVE` and
  `GAMEPAD_LIST_ANNOUNCED=1118/654` at 14 s,
  `GAMEPAD_CHANNEL_SEND_OK_NOT_DELIVERY_PROOF`, then `AUTO_ENTRY_ARMED` and
  `AUTO_ENTRY_TAP_SENT=1` at 68 s, `GAME_SDK_WILL_ENTER_GAME` at 71 s and
  `AUTO_ENTRY_GAME_ENTERED_STOPPED`. 4157 decoded frames, 0 decode errors,
  7533 audio packets. Outcome `TestEndedNotGameplayProof` (the 182 s autotest
  limit), no failure event. The 2048-entry safe-log ring was needed because a
  512-entry ring hid the earlier entry/pad events on healthy long runs.
- Host tests: 51 PASS / 0 FAIL, including 4,000,000 bit/s dispatch, StopGameRsp
  command decoding, monitor failure classification and translation keys.

## Controller response verified in yuzu; attached Joy-Con handling (0.2.63)

This section supersedes the older claim that CaGameConfig=0 proves pod input
rejection or that a missing 20037 mode notification proves a broken controller.

- Official 7.1.0 `J5.a.k()` adds `idg` to dispatch `user_data`; the old command
  line equivalent is `-is_default_gamepad 1` in `J5.a.N()`. Native launch now
  sends `idg=true` for hk4e only, signed as part of user_data. No invented
  StartGameReq fields or local daemon commands are used.
- A terminal-10 comparison restored login without QR, entered the real game,
  rotated the camera through type-17 right-stick input and opened the Paimon
  menu through the pad Start button. Frames 7/8/9 in
  `outputs/debug-0.2.62-idg-web-pad-trial3-20261023` show those changes.
  That run decoded 4345 frames with zero decode errors and received 7607
  audio packets. All three server gamepad flags remained zero; 20037 mode 3
  was not observed. Neither is a reliable failure gate on this path.
- Terminal 3 returned all three pad flags enabled but no media before the
  60-second first-frame deadline. It is NOT the release default. The supported
  configuration remains terminal 10, Xbox 360 identity, native pad frames;
  there is no keyboard/mouse gameplay mapping in these successful probes.
- Desktop title entry now uses the proven mouse click, independently of the
  gameplay route. Mobile terminals keep touch. Every entry path releases its
  pointer even if the SDK enters during the hold. SDK will_enter_game disables
  further title taps; real button edges can retry an early loading-screen tap.
  Default auto-entry waits for the same 55-second stream gate as the verified
  probe, instead of treating the initial logo fade as a successful entry.
- Attached Joy-Con use an explicit Handheld reader, with priority over external
  controllers. Detached paired Joy-Con / Pro controllers use one stable player
  source. Inputs are never merged between players. A source change/disconnect
  requires a successfully queued neutral pad report before new held input;
  failed writes retry. Sticks use a 12% radial deadzone without smoothing.
- Marker-only input probes feed the normal UI/overlay/input route and record
  bounded decoded-game luma images. They test camera and menu only, never guess
  a face-button confirm/cancel mapping (the account may swap those). Release
  packages contain no test markers, credentials, decoded images or ELF.
- The test monitor no longer aborts endpoint failover on one failed signaling
  attempt. An exhausted worker still fails; successful upgrade clears only the
  pending attempt failure, not independent RTC/control failures.

Host checks cover dispatch signing/flow, every button/trigger mapping, touch
goldens, controller-source priority, deadzone extremes, neutral handoff retries,
overlay release, RTC transport and translations. The actual devkitA64 NRO was
built, and a 0.2.63 repeat decoded 4553 frames with zero decode errors and 7991
audio packets. These are yuzu/cloud-input results, NOT physical Joy-Con testing,
audible-output validation or a multi-game completion/stability claim. The yuzu
probe injects semantic pad state after local HID sampling; it cannot substitute
for attached hardware. See `controller-fix-0.2.63-2026-10-23.md` for handoff.
The final packaged hash's repeat runs subsequently received command 20005 and
lost signaling at 71 s / 21 s, before reaching the pad probe. Do not report
those as passed input/stability runs. The earlier visible input evidence stays
separate from this unresolved session-lifetime issue; the monitor now fails an
unexpected SIGNALING_DISCONNECTED event.

## Virtual-pad identity override and Android terminal probe (0.2.62)

The pod protocol carries the virtual pad's **vendor/product pair**, not a
display-name string: every `RtcGamepadList` entry and every type-17 frame has
vendor_id/product_id, and the pod's driver derives the device name from that
pair. 0.2.62 therefore makes the pair configurable instead of adding a name
field the pod would ignore.

`cloudgenshin-pad-id.txt` accepts either a profile name or a hex pair:

```
xbox360      -> 045e 028e   (default; Xbox 360 Controller)
xboxone      -> 045e 02ea   (Xbox Wireless Controller)
xboxseries   -> 045e 0b13
ps4          -> 054c 09cc   (DualShock 4)
ps5          -> 054c 0ce6   (DualSense)
switchpro    -> 057e 2009
logi         -> 046d 0af9   (Logitech GR0006)
045e 028e    -> explicit hex pair
```

`GAMEPAD_IDENTITY code=<vid> code2=<pid>` records the chosen pair at stream
start, and `GAMEPAD_LIST_ANNOUNCED` / `GAMEPAD_LIST_REANNOUNCED_ON_FIRST_PRESS`
carry the same pair, so one log line proves what the pod was told. The default
stays Xbox 360; the marker is only for the identity experiment.

The dispatch request now carries the official launch-time default-pad hint:
for hk4e it sets `using_new_cmd_line=true` and puts `idg=true` in `user_data`
(the legacy spelling was `-is_default_gamepad 1`). This is a dispatch-time
preference only; it does not change the launcher pointer path. The launcher
pointer is terminal-aware: touch-first terminals (2/3/6/7/9) send the official
touch stream, while desktop terminal 10 sends mouse clicks because desktop pods
consume mouse on the title overlay. 0.2.62 therefore keeps the default touch
entry for Android/touch terminals and still leaves every gameplay frame on the
virtual pad.

The server-side `CaGameConfig gamepad_enable_*` flags come from the terminal
type, not from the client. Terminal 10 (web keyboard) returned all three flags
off; terminal 3 (Android) is the next probe. The terminal override is read in
`cloudgenshin-terminal.txt` at stream start and only affects `StartGame` and
the SDK CPS string, not the earlier login/QR/queue requests. Keep 0.2.61's
touch hardening and default launcher auto-entry unchanged: the launcher path
stays pointer-first, and every gameplay frame is still a pure virtual pad.

## Touch hardening and default launcher auto-entry (0.2.61)

Touch is the foundation of the launcher path, so 0.2.61 makes the existing
official TouchController stream observable and loss-resistant before adding
automation:

- Every manual touch batch that is actually handed to SCTP logs
  `TOUCH_SENT_DOWN`, `TOUCH_SENT_UP` or (throttled) `TOUCH_SENT_MOVE` with the
  normalized coordinates as per-mille integers. A live tap can therefore be
  checked from `cloudgenshin-safe.log` without exposing pixels or tokens.
- A failed touch batch is put back in front of newer state and retried on the
  next 16 ms frame, so an Up cannot be dropped silently and leave the pod
  holding a contact. Batches are sent in ordered 10-event chunks (the wire
  limit), so a retry plus newer state cannot wedge the queue. The one-shot A
  tap and the auto-entry both retry their release for the same reason.
- The wire format is unchanged from 0.2.59: `RtcDataChannel` type 21, field 13
  `RtcTouchEventList`, `point_x/point_y` as fixed32 floats normalized to
  [0,1], action 0/1/2. The host goldens pin the exact bytes.

Default launcher auto-entry now runs without the autotest marker. It arms only
3 s after the first decoded video frame (never during login, QR or queue), waits
for a fresh non-black luma sample, then taps the title page's "点击进入" bar at
(0.5, 0.947) with a 120 ms hold. Before the next tap it waits for a luma sample
taken after the previous tap; the sequence stops on a scene change of at least
15 luma units, any real touch, any physical pad press, or after 10 taps. The
`cloudgenshin-auto-entry.off` marker restores the fully manual path. The
autotest entry probe still disables the default sequence to avoid double taps.

The entry tap is only a pointer event on the cloud launcher overlay. It does
not change the game version or lock it into touch mode: the pod still runs the
PC (terminal 10) client, and the game switches its control mode from the last
input device once the pod has created the virtual Xbox pad. `20037` remains a
server-to-client report, so there is no client-side "force gamepad mode"
message to send.

## Launcher A shortcut and resilient pad announcement (0.2.60)

The streamed title page is the cloud client's pointer-only overlay: the game
process and its SDK may already be running behind it, but the overlay is the
focused window and does not consume XInput, so a face-button press never
dismisses it by itself. 0.2.60 adds a deliberately narrow shortcut: before the
first real touch, the first face-button press sends one touch tap on the
"点击进入" bar at (0.5, 0.947) and releases it 120 ms later. The shortcut is
consumed once; after any real touch it is disabled, and every gameplay frame is
still a pure virtual Xbox 360 pad frame. There is still no keyboard/mouse
gameplay mapping.

The pad announcement is also retried until the SCTP write actually succeeds and
is re-sent after the input channel recovers. The earlier implementation set its
one-shot flag before checking the send result, so a transient failure could
silently skip the virtual-pad list for the whole session. The 20 s
`GAMEPAD_SERVER_MODE_NOT_REPORTED_20S` watchdog now starts at the first real
press rather than at channel open.

## Touch float fix kept, control-mode direction corrected (0.2.59)

The 7.1.0 APK descriptors (`docs/cloudgenshin-protocol-schema-7.1.0.md`) settled
two encoding facts that the earlier runs could only guess at:

- `common.RtcTouchEvent.point_x/point_y` are **float**, not double. Every touch
  frame sent before 0.2.58 used wire type 1 (fixed64), so the pod parsed the
  event with `point_x/point_y = 0` and read it as a tap in the top-left corner.
  This alone is enough to explain a launcher that never advances on touch.
  `EncodeTouchEventListChannel` now writes fixed32, pinned with new goldens.
- `common.SdkGameConfig` (StartGameReq field 12) only has
  `enable_wrapped_h265` and `is_big_isp`. There is no gamepad field, so the
  0.2.56 field-12 guess is permanently withdrawn.

0.2.58 also tagged every frame with `RtcDataChannel.input_control_type` and sent
`SyncGameControlMode` (20037) upstream. Disassembling the unpacked
`libclient_core_impl.so` (yscloud 7.1.0) showed both changes were wrong:

- `SyncGameControlMode` is decoded inside `ClientCoreImpl::dispCmd`, i.e. it is
  **server -> client**. The CA logs `sync game control mode: %d` and forwards
  `{"control_mode":"gamepad"|"default"}` to the app through `onGameInfo -1039`.
  Sending 20037 upstream was a no-op and is removed; the client now decodes it
  and reports `SERVER_GAME_CONTROL_MODE code=N` (3 = the pod switched to pad).
- `input_control_type` (field 17) is a **bitmask** of `ClientCore.GameContorlType`
  (`KEYBOARD=1, MOUSE=2, GAMEPAD=4, TOUCH_SCREEN=8`), used only on the type-31
  `RESET_GAME_INPUT_CONTROL` frame. `onGamepadPressed` (type 17),
  `onGamepadDevices` (type 18) and `sendTouchEventList` (type 21) never write
  it, so 0.2.59 stops tagging regular frames. `EncodeResetGameInputControlChannel`
  implements the official type-31 frame for the cases where the client must
  clear held input on the pod.

The touch float fix is unaffected and remains the one confirmed input bug fixed
in this chain.

0.2.59 also re-announces the same `RtcGamepadList` on the first physical button
press (`GAMEPAD_LIST_REANNOUNCED_ON_FIRST_PRESS`), so a pod input adapter that
finished initialising after the channel-open announcement still sees the device
at the moment it is used. There is no keyboard/mouse mapping fallback: a
controller press either produces virtual-pad frames or nothing.

The server gate itself is unchanged: `StartGameRsp.CaGameConfig`
`gamepad_enable_xbox` is still 0 for this account and the session keeps
force-enabling the local Xbox route (`GAMEPAD_SERVER_CONFIG_OFF_FORCED`).
Whether the pod honours type-17 frames without that flag is still unproven;
`SERVER_GAME_CONTROL_MODE` now gives a direct receipt when it does.

## Controller identity and the gate history (0.2.51-0.2.59)

0.2.50 announced `RtcGamepadList` and sent RtcDataChannel type-17 frames even
when `CaGameConfig` left both virtual pad flags off, and removed the
`Session::send_gamepad` gate. That run never left the pod launcher, but the
launcher is pointer-only by design and that build's touch frames used the wrong
wire type, so the run did not prove the pad frames were rejected. 0.2.51
reverted to the official precondition while keeping the parts that are
independent of the server gate:

- The virtual pad identity is the Xbox 360 Controller (vendor 0x045e /
  product 0x028e), the identity whose XInput virtualization the pod already
  documents; a Switch Pro identity would need a different virtual device
  profile.
- Disassembly of the official APK shows `ClientCore.startGamepad(...)` only
  starts the local Java `GamepadManager`; no client-to-pod message carries
  `gamepad_enable_*`. 0.2.54 therefore re-forced the Xbox route: the list and
  type-17 frames are sent regardless of the server flags, and
  `GAMEPAD_SERVER_CONFIG_OFF_FORCED` records the decision. 0.2.59 keeps the
  force, re-announces the list on the first physical press, and reports
  `SERVER_GAME_CONTROL_MODE` when the pod acknowledges gamepad mode.
- `GAME_CONFIG_XBOX/PS/GR0006/WEBCTRL` stay in the safe log.
- The launcher always rides the touch stream; the pad is only expected to
  drive gameplay. No keyboard/mouse mapping fallback is built: a controller
  press must produce pad frames or nothing.
- The default CA terminal type is CTT_WEB_KEYBOARD (10), matching the official
  H5 desktop page; `cloudgenshin-terminal.txt` overrides it for diagnostics.
- Host goldens pin the Xbox identity and the 0x045e/0x028e list bytes. The
  RTC-session mock covers both configs: a `CaGameConfig` with the Xbox flag on
  forwards frames, one with both flags off still forwards them on the forced
  route.

Whether the pod creates and honours the virtual pad without the server flags is
the remaining live question, not a proven rejection. `SERVER_GAME_CONTROL_MODE`
code 3 is the direct receipt; `GAMEPAD_SERVER_MODE_NOT_REPORTED_20S` appears
when a physical press produced frames but the pod never reported pad mode. The
open question is why this account's `CaGameConfig` keeps the flags off; a
session where the flags are on would settle it immediately.

## Server-directed media renegotiation (0.2.49)

The official web client keeps one RTCPeerConnection and answers every later
server offer. `Session::handle_signal` previously ended the stream with
`RTC_RENEGOTIATION_UNSUPPORTED` as soon as a second offer arrived. A repeated
offer that keeps the remote ICE ufrag and DTLS fingerprint is now treated as a
same-transport media update: libpeer applies the new SSRCs in place through its
existing `is_update` path, and the client answers with its established local
description instead of asking libpeer to build a new one. This matters because
libpeer's answer builder rotates local ICE credentials and the DTLS certificate;
that is only valid for an ICE restart, so restart offers still end the session
with the fixed unsupported event. Accepted updates are capped at 32 per session
(`RTC_RENEGOTIATION_LIMIT_EXCEEDED` beyond that) so a malfunctioning pod cannot
drive unbounded work; each accepted update emits `RTC_RENEGOTIATION_ACCEPTED`
with its running count. Host test `mihoyo_rtc_session` covers update acceptance,
the cap and the restart rejection. Live server behavior is not yet verified.

## Game touch forwarding (0.2.48)

RtcTouchEvent frames (DataType 21, field 13) are now sent from the live
stream instead of only being encoded. `app/src/mihoyo/touch_forward_policy.hpp`
tracks pressed contacts per UI frame with PointerEvent isPrimary semantics,
releases vanished fingers first, caps each batch at the official 10 events
while keeping the newest positions for the following 16 ms send, and releases
every contact while a UI panel owns the screen or the stream exits. The
floating ball keeps its first-pressed finger; every other contact is
forwarded. Touch is also the only way past the pod launcher; 0.2.58 fixed the
float wire type, and the stream forwards contacts whenever the overlay does
not own them. Host test: `mihoyo_touch_forward`. In-game delivery of forwarded
contacts is still unverified.

## SDK bridge contract completion and control-channel fallback (0.2.32)

The official Combo web clients (`combo-web` for Genshin and Star Rail) expose
`login_get_asterisk_name` and `login_has_scan_func` through the same dispatcher
as the other `login_*` handlers. The native bridge now answers them with the
official values: `login_has_scan_func` returns `String(qr_enabled || true)`
(always `"true"`), and `login_get_asterisk_name` returns the masked name from
`user_info` (mobile, email, third-party nickname or account name) or an empty
string when no `user_info` exists. Returning `"{}"` here made the game web page
treat the account as malformed.

For sessions whose game configuration selects the independent
`GameControlChannel` (`webControlChannelType` 1/2), GCC health is reported only
through the `CONTROL_WSS_*` events. The SCTP control policy is still tracked as
the official fallback transport but no longer emits `RTC_CONTROL_*` status
events, so a missing association cannot overwrite the visible status. A dead
control WebSocket now degrades to the RTC data channel (`sendRtcData`), exactly
like the official web SDK, instead of ending the stream.

Autotest gained an explicit, opt-in entry click: when
`cloudgenshin-autotest.click` contains `CGNX_ENTRY_CLICK_V1`, the session sends
one controller click at the frame centre ten seconds after `GAME_SDK_LOGIN`,
through the same `request_continue()` path a menu press uses. Without the marker
the client never clicks. This only exists to verify "click to enter" without a
human at the console; the production path is unchanged.

Host tests were refreshed against the current protocol: the input-packet test
now asserts the official `cg-sdk` DataType map (`MOUSE_LBUTTON_DOWN` 4 ...
`MOUSE_RBUTTON_UP` 7) with `RtcMouse.cursorX/cursorY` doubles, `Authorization`
initializers carry `channel_token`, and the RTC-session test expects the
control-channel fallback semantics above. The miHoYo host suite passes.

## Genshin low startup preset and video progress recovery (0.2.25)

The 0.2.24 field run continued for about 175 seconds and ended by user request,
but included intervals with incoming video and no new decoded pictures. This
does not identify the game UI/loading state; the user reports no loading bar.
Startup now explicitly requests extreme-saving mode / 30 FPS after connection
readiness. The official proxy KeyFrameReq (20032) supplements RTCP PLI, with a
bounded no-output watchdog, IDR retry/resync and clearer fresh/reused-frame
diagnostics. Static scenes with advancing decode counters do not trigger resets.
Safe log snapshots retain bounded startup evidence plus recent counters.
See [scope and device checks](genshin-startup-video-recovery.md).

## Genshin independent game-control transport (0.2.24)

The supplied 0.2.23 Switch safe log had successful DTLS, 720p video decoding
(711 frames, no decode errors) and audio, then stopped at the 15-second SCTP
association deadline. Its game config selected `webControlChannelType=2`.
The local official web SDK uses an independent GameControlChannel for types
1/2, with an explicitly supported WebSocket fallback. The native session now
implements that transport, including dispatch/server-index pairing, the
signaling handshake session id, binary sync/ack, ping/pong, and routing of
input and game heartbeats. SCTP remains enforced for other control types.
Sensitive session ids/URLs stay in memory; logs contain fixed event names and
numeric transport status only. See [protocol, tests and device check](genshin-web-control-fix.md).

## Endfield QR account-request follow-up (0.2.23 account-fix)

The latest safe field log reached the post-scan UID lookup and recorded HTTP
400, conflating the grant/binding step and HTTP/local code. Account requests now
share the SDK device-header shape and one process-lifetime random client id
through QR creation/exchange, OAuth grant and binding calls. Grant query values
are URI-encoded; parsing requires an explicit success status. Role UID lookup
is cancelled with the scan job and is resolved from the game's binding list.
Safe failure events distinguish the grant and binding stages with separate
numeric code/http fields. See [repair and handoff notes](endfield-qr-account-fix.md).

Three offline test groups pass (account policy, synthetic HTTP account/QR chain,
UI language), as do the devkitPro native build and final incremental rebuild.
No real account, emulator or queue was used this round; whether the real HTTP
400 is eliminated still needs a phone-confirmed login with the account-fix NRO.

## Endfield stream view + device-session start flow (0.2.22)

Wires the previously gated Start button to the full browser-path cloud flow
(no behaviour of the mihoyo path is changed):

- `endfield_stream_view.{hpp,cpp}`: a trimmed port of the CloudGenshinStreamView
  structure — MediaPipeline (decode/audio) + Deko3D renderer + worker thread —
  owning one `VlinkSession`. The worker drives `poll()`, applies the 省流 30 FPS
  preference once the welink channel opens (largely a post-start SetFps, per the
  profile notes), detects first decoded frame/game-start, snapshots diagnostics
  every 5 s, and stops with a best-effort neutral-input release. Video/audio
  packets from the session go straight into the pipeline.
- Input: 60 Hz pad sampling in draw(), 16 ms worker cadence; only diffs are
  sent as welink XInput ops (buttons mask, triggers, four axes). The
  position-preserving mapping reuses the shared `ToGamepadState` encoder, whose
  bit table is byte-identical to the official Endfield XInput table. Minus is
  reserved for the local diagnostics panel; hold + and - stops and returns.
- Tab start flow (`endfield_start_worker`): vault token → `api::Exchange`
  → deploy node list (ordered by delay) → `/session/dispatch` → poll every 2 s
  (100 ms cancel slices) emitting `QUEUED_POSITION` → parse `sdk_msg`
  (`ParseVlinkSdkMsg`) → store under the mutex → draw() pushes the stream view
  once. Cancel before/after dispatch calls `/session/cancel`. The primary
  button adapts to a third mode: Cancel while the flow runs; switching games
  stays blocked while busy; switching tabs or clearing the vault cancels the
  flow best-effort and a new start joins the finished thread.
- The autotest one-shot opt-in (same `CGNX_AUTOTEST_V1` marker file as the
  mihoyo queue) also starts one Endfield session from a ready vault — this is
  how the flow is exercised headlessly in the debug emulator; the view's
  automated 110 s limit stops it.
- Status/diagnostic keys reused where possible; new keys:
  `ENDFIELD_STREAM_LOGIN_FAILED/NODES_FAILED/DISPATCH_FAILED/QUEUE_FAILED/
  SDK_INVALID/STARTING/CONNECT_FAILED` plus the diagnostics help string.

## Four official per-game backgrounds (0.2.21)

- One official background per game, swapped on game selection; only the
  selected game's NVG texture stays loaded (old image deleted before the new
  one is created). Assets and provenance recorded in
  `resources/img/CLOUD-BACKGROUND-SOURCE.md`.
- Boot-checked on the debug emulator per game (hkrpg / nap / endfield captures
  in reports/bg-pergame-02121) and via the device run logs (bg-02121).
- Packaged 0.2.21 NRO under
  `outputs/CloudGenshin-NX-0.2.21-four-backgrounds/` (mirrors the 0.2.20
  package layout).

## Device session notes (0.2.20 logs from the Switch)

- `cloudgenshin-safe.log` (0.2.20 on hardware): clean boots, Endfield QR login
  succeeded (`ENDFIELD_LOGIN_OK`, vault + key now saved on the SD), then
  `ENDFIELD_STREAM_PENDING` — the then-expected placeholder this revision
  replaces. Mihoyo sign-in was cleared on the device (`cloudgenshin-login.disabled`).
- The 0.2.12 device log remains the reference for a fully working mihoyo
  stream on hardware (decoded video, audio, RTC connected). The 0.2.17+
  mihoyo `media=0` regression is still unexplained.


## Minimal main page (0.2.19)

Native-build main-page revision (the `CLOUDGENSHIN_NATIVE` build hosts this
page directly). No protocol, endpoint, credential or queue behaviour changed.

- The on-screen safe-log text, the export-log button, the long game help copy
  and the duplicate Endfield heading are removed from the page. The same
  events still auto-save to `cloudgenshin-safe.log` and the `ENDFIELD_*`
  files on SD, so diagnostics for bug reports are unchanged.
- The page is now one column: wordmark and language toggle, four equal game
  tabs, a compact status card, one game card and a version/exit footer.
- The status card shows friendly bilingual states (signed in, waiting for
  scan, joining the queue, choosing a server, errors) with a state dot and,
  while queuing, the live position, estimate and progress. Raw event keys are
  no longer shown to the user.
- Each game card has one adaptive primary action (Start when signed in,
  Sign in when signed out, Cancel while running) plus the secondary actions
  for that game (sign out / refresh QR / clear login) and a QR or signed-in
  tile. Controller focus uses a border highlight instead of a filled
  background.
- QR refresh/clear, saved-login restore, start/cancel, sign-out and the
  Chinese/English toggle keep their existing logic. The Endfield QR still
  starts automatically when its panel is selected and no vault exists.
- The Endfield Start action remains gated (`ENDFIELD_STREAM_PENDING`) until
  the stream view/input bridge is wired; the card reports this as a friendly
  "stream coming soon" state rather than a raw key.
- Not yet validated on real Switch for this revision; the device pass remains
  the acceptance gate.

## Touch floating ball and stream settings (0.2.16)

This is an offline UI/settings update, not a new live-game acceptance result.
No emulator, login restore, queue, cloud session or scheduled job was started.

- A vector floating ball is visible over the stream. Tap it to open settings;
  drag it to reposition and snap to either edge. Its single-finger gesture owns
  input until release; dragging, changing fingers and touches begun elsewhere
  cannot turn into an accidental activation. Hold L+R for 800ms to open the same
  panel with a controller. The diagnostic panel has a settings button as well.
- The panel offers the three existing official graphics tiers and 30/60 FPS,
  a performance HUD, Chinese/English switching, diagnostics, safe-log export,
  explicit one-click continue, and a two-step stop confirmation. D-pad up/down
  selects, physical A activates and B closes/cancels confirmation. Closing the
  panel or diagnostics waits for buttons/sticks/touch to be released before
  forwarding gameplay input. No automatic gameplay clicks are added.
- Readouts use decoded frame dimensions, a one-second decoded-frame delta and
  received completed-video payload bytes, plus decode drops/errors and actual
  input readiness. They are not display refresh rate, RTT, total wire bandwidth,
  or proof of gameplay progress. No endpoint/account data enters the panel.
- Graphics/FPS changes remain worker-owned signaling proxy requests using the
  existing protocol. The UI distinguishes queued, locally sent and failed;
  locally sent is explicitly NOT server-confirmed/applied. Revision-tagged
  atomic requests prevent a late result from overwriting a newer choice.
  Graphics starts with the existing traffic-saving default. Settings and ball
  position are per-session; the existing language preference still persists.
- Panel drawing/input lives in cloudgenshin_stream_overlay.cpp; testable gesture,
  layout, rate-sampling and request policies live in stream_overlay_policy.hpp.
  Hidden diagnostics stay hidden and the unobtrusive game hint fades after 8s.
  No unsupported arbitrary resolution, bitrate, RTT, volume or codec settings
  are invented. Login, RTC negotiation, media queues and decoder selection are
  not changed by this update.

Seven related host test groups pass: overlay policy, input policy, language,
game-control encoding, RTC session, diagnostics policy and safe events. The
devkitPro Switch build passes; release artifacts and final metadata are checked
when packaging under outputs/CloudGenshin-NX-native-alpha-0.2.16 in the parent
workspace. Real Switch touch layout and the server's response to quality/FPS
changes still require device/live validation; the update does not claim that
the title-screen/control-channel issue is solved.

## Offline control/input reliability fixes (0.2.15)

This round deliberately does not start an emulator, restore an account, dispatch,
queue or connect to the cloud service. It repairs code-review findings only;
passing offline tests must not be presented as getting past the live title page.

- DTLS/video completion is separate from control readiness. SCTP initialization
  results are exported numerically from libpeer without changing the GFN peer
  lifecycle. Native sessions fail explicitly on initialization failure, a missing
  association after 15 seconds, a missing DCEP channel after another 15 seconds,
  or loss of an established association. Input and heartbeat require both SCTP
  and the remote channel. The first-heartbeat event is committed only on a
  successful local send, so a transient first failure does not suppress it.
- Delivered key/mouse state is committed only after a successful local send.
  Failed down/up transitions remain pending. The independent policy has no UI
  or network dependencies, and session timeouts use an injectable monotonic clock.
- Unconditional first-frame and repeated wake clicks are removed. The diagnostic
  panel offers “Continue / send one click”; only that user request initiates a
  bounded move/down/up sequence after video and input are ready. A failed step
  retries that step rather than another whole click; release debt is retried at
  a bounded rate. A send is not proof that the server accepted the action.
- L+R is captured from the first combined press, including the 800ms hold window.
  Closing menus/diagnostics waits for physical controls to be released before
  forwarding gameplay input. Frame-sample counters are atomic. Hidden diagnostics
  no longer translate/rebuild log labels every 250ms; reopening refreshes them.
- Input handling lives in cloudgenshin_stream_input.cpp. Existing per-game login,
  wire formats, RTC negotiation, rendering and decoder choices are preserved.
  App/CMake versions are synchronized to 0.2.15. Field SCTP diagnostics are reset
  at session teardown, rather than remaining globally enabled for the next run.

Eighteen related offline checks pass: twelve C++ test groups (control policy,
input policy, RTC session, language, login storage, launch flow, game profiles,
input encoding, game-control encoding, SDP answer, diagnostics policy and safe
events), the PowerShell safe monitor, and five C transport/DTLS tests. The
devkitPro Switch build and final incremental rebuild pass; NRO0/ASET metadata
identifies CloudGenshin-NX alpha 0.2.15. Matching NRO/ELF and notices are packaged
under outputs/CloudGenshin-NX-native-alpha-0.2.15 in the parent workspace.
No emulator or live-game acceptance was performed for this version.

## Multi-game profiles: 云·原神 / 云·星穹铁道 / 云·绝区零 (0.2.11)

The launcher now has an in-app game selector backed by the `GameProfile` table in
`app/src/mihoyo/production_profile.hpp`; the selection persists to
`cloudgenshin-game.txt`. The encrypted SD login store is version 2 (one entry per
game) and read-migrates existing single-game vaults; a login-save failure in an
earlier multigame build was a JSON serialization bug, fixed before 0.2.11 shipped.
Games are switched while logged out (switching performs a full logout of the
previous game). Per-game constants come only from official artifacts:

- **云·原神 (hk4e)** - existing constants from the official CN web client bundles
  (`reference/web/ys`); app_id 4, `hk4e_cn`, `clgm_cn`,
  `api-cloudgame.mihoyo.com/hk4e_cg_cn`.
- **云·星穹铁道 (hkrpg)** - official CN web client bundles (`reference/web/sr`);
  app_id 8, `hkrpg_cn`, `clgm_hkrpg-cn`, `cg-hkrpg-api.mihoyo.com/hkrpg_cn/cg`.
- **云·绝区零 (nap)** - official 云·绝区零 Android package
  (`reference/android/ZenlessZoneZeroCloud_3.2.apk`): `assets/server_env_config.json`
  env 0 (app_id 12, `nap_cn`, channel 1) and dex string evidence for
  `cg-nap-api.mihoyo.com/nap_cn/cg`, `x-rpc-*` headers and `clgm_nap-cn`. The
  `sdk_version`/`app_version` pair (6.3.0.15 / 3.2.0) is pending official-app
  confirmation.

Field test of the delivered 0.2.11 NRO (user safe log, 2026-10-16): 云·原神
restored its saved login at boot; 云·星穹铁道 completed a fresh QR login (scan and
phone confirm), saved it (LOGIN_SAVED), and both games later reached the normal
queue (positions 371 and 485). 云·绝区零 completed QR fetch / scan / phone confirm
and Combo exchange (HTTP 200), but its cloud account check (`gamer/api/login`)
returned retcode -100002, on the first attempt and on a later retry.

-100002 evidence: the official web clients classify -100002 in the cloud-game
error enum as `WHITE_LIST` (their handler shows the login-failed dialog with the
server message, then logs out); account ban (-100004) and login frequency
(-100003) are different codes. A controlled probe with an intentionally invalid
Combo token (no account material) returned -100 TOKEN_INVALID, so the server
accepts our request shape/identity headers and the -100002 decision happens after
token validation as an account/service-state gate.

## 云·绝区零 request identity vs the official app (0.2.12)

The official 云·绝区零 Android package (`reference/android/napcloud-3.2-jadx`,
jadx of the 3.2 APK) serves every cloud-game call through one OkHttp client
(`A4/h.java`) whose `LoginStatusInterceptor` (`p123i4/c.java`) attaches the
`utils.a.p0()` header map plus the Combo token. For `gamer/api/login` the
release app therefore sends, among others:

- `x-rpc-client_type: 2` (Android app), `x-rpc-app_version: 3.2.0`,
  `x-rpc-channel`/`x-rpc-cps` = channel value, default `mihoyo` (`utils.a.y()`),
  `x-rpc-vendor_id: 2` in release builds.
- `x-rpc-app_id` = the app-upgrade application id `1953458679`
  (`p054d5.a.f21657m`, wired into `AppEnv.AppUpgrade` at `p050d1/c.java:49`),
  not the Combo app id 12.
- `x-rpc-preview` / `x-rpc-cover-session` are only added when the debug-panel
  switch is on (`i0.A` reads `enableDebugPanel`, false in this release), so the
  release app sends neither.

Our 0.2.11 request differed in exactly three values: `client_type` 19 (shared H5
identity), empty `x-rpc-cps`, and `x-rpc-app_id` 12 (Combo id). Since 云·绝区零
has no H5 client, the server's whitelist likely does not accept the H5 identity
for it. 0.2.12 adds per-profile identity fields (`client_type`, `cps`,
`cloud_app_id`): 云·绝区零 mirrors the official app (`2`, `mihoyo`,
`1953458679`), while 云·原神 / 云·星穹铁道 keep the field-proven H5 identity
(`19`, empty cps, Combo app id). Host tests pin the new wiring; the real-device
round remains the acceptance gate.

## In-stream input, wake click and quality switching (0.2.13)

User-supplied 0.2.12 safe log (2026-10-17): after STREAM_ENTRY the 云·原神 pod
delivered live video for ~70 s, then VIDEO_UNITS_RECEIVED froze on a static
title/continue screen while the renderer kept re-submitting the last frame and
VIDEO_DECODE_ERRORS stayed 0. No input frame had ever been sent: the server's
caGameConfig kept gamepadEnableXbox/Ps off (GAME_AUTH_OK_GAMEPAD_DISABLED), and
the official web/Android clients gate their gamepad path on exactly those flags.
0.2.13 adds the official fallback paths instead of fighting the gate:

- Keyboard/mouse emulation: Switch inputs are translated to RtcKeyboard/RtcMouse
  frames on the same RTC data channel (command 20010) as gamepad frames —
  physical A -> LMB attack, B -> Space (jump), X -> E, Y -> Q, left stick ->
  WASD with dead zone, right stick -> relative mouse look, dpad -> 1..4 party
  switch, L -> Shift, R -> F, ZL -> RMB, ZR -> LMB, plus -> Esc. Frames are
  only emitted on state changes (KEYBOARD_MOUSE_INPUT_ACTIVE reports first send).
- Wake click: after the first decoded frame the client sends a relative move
  plus a left click (INPUT_WAKE_CLICK_SENT); if the picture stays frozen it
  repeats at most four times six seconds apart (INPUT_WAKE_CLICK_RESENT) and
  reports VIDEO_RESUMED_AFTER_INPUT when video advances again, or
  INPUT_WAKE_NO_VIDEO_RESPONSE when it never does. The official web client has
  the same click-to-continue behavior; the frozen 0.2.12 field log matches a pod
  waiting for that input.
- Quality / frame-rate switching: holding L+R opens a small overlay
  (高清 / 省流 / 极省流 / 帧率 30 / 60). The default request is 省流
  (STREAM_QUALITY_DEFAULT_TRAFFIC_SAVING), sent once after authentication;
  selections send RtcSetGraphicsMode (20036) / RtcSetFps (20022) over the
  signaling proxy channel, the same transport the official floating ball uses.
- Gamepad handling keeps its official form: send_gamepad returns false unless
  caGameConfig enabled a pad; a RtcGamepadList frame (Switch Pro
  0x057e/0x2009) is announced once when the remote data channel opens
  (GAMEPAD_LIST_ANNOUNCED), and GAME_CONFIG_XBOX/PS/GR0006/WEBCTRL are logged
  after authentication so the server gate is visible in the safe log.

Host tests pin the new encoders with golden bytes; the real-device round
remains the acceptance gate.

## Input-channel gating and SCTP instrumentation (0.2.14)

The 0.2.13 yuzu GUI stream (run 1792204748, dispatched at t=168 s) ended with
healthy video (6029 units / 6020 decoded / 0 decode errors at t=278 s) but its
safe log contained no `RTC_REMOTE_DATA_CHANNEL_OPEN`, `GAMEPAD_LIST_ANNOUNCED`,
`INPUT_WAKE_CLICK_SENT` or `KEYBOARD_MOUSE_INPUT_ACTIVE`: the pod-opened SCTP
data channel that every 0.2.13 input path is gated on (`Session::poll`
heartbeat, `has_remote_data_channel()`) never surfaced, so no input reached the
pod — consistent with the user-reported stuck opening/loading screen. 0.2.14
makes that layer observable and completes the click:

- libpeer: new `peer_connection_sctp_connected()` reports the SCTP association
  state (no wire-format change).
- Session: `RTC_SCTP_ASSOCIATION_UP` fires when the association comes up;
  `CONTROL_HEARTBEAT_STARTED` fires when the first data-channel heartbeat is
  actually sent to the remote channel.
- libpeer SCTP/file diagnostics are enabled only while the debug autotest
  marker exists, and the legacy home directory they write to
  (`sdmc:/switch/OpenNOWSwitch`) is created first — the files had silently
  failed to open, which is why no SCTP trace was ever available.
- The wake click now sends a full down+up pair; the press was previously never
  released, which the official model does not treat as a click.

Queue congestion on 2026-10-17 11:00–11:59 kept every unattended retry from
dispatching (positions ~126–240; full runs ended with a clean AUTOTEST_TIMEOUT /
LAUNCH_CANCELLED / QUEUE_LEFT, and one retry ended early when the yuzu-cmd
process exited by itself mid-queue with exit code 0). A stream run is still
required to confirm the association/channel events and whether the pod then
proceeds past its opening sequence; the next session resumes from here.

## Frame-content verification and 110 s stream window (post-0.2.11 dev)

A decoded-frame content sampler now runs in the stream view: every five seconds
it reads a sparse 64x36 grid of the exact luma plane handed to the renderer and
logs numeric-only events (VIDEO_FRAME_SAMPLE_LUMA_MEAN/MIN/MAX, _CONTRAST,
_NONBLACK, _COUNT). Values only; no pixels are stored, exported or logged. The
safe-event ring is 512 entries and the host monitor allowlist now carries the
sample names, so a full run keeps every stage event.

The automated stream window is 110 s (was 60 s) so a slow queue still leaves
time for the cloud client's startup page to become real game video. Harness
budgets must exceed the window: a previous 110 s budget cut a slow-queue run
(status position ~40, dispatch at 94 s) after 11 s of stream and left a stop
marker behind; use RunTimeoutSeconds well above the window.

Local yuzu EA4176 verification (report rtc-0.2.11-content-02, dispatch at 74 s,
183 s, TestEndedNotGameplayProof): zero decode errors, 22 samples over the
window - flat white startup page (mean 234-235, contrast 0) through the load
transition (227/8, 170/13, 136/18) into sustained game imagery for the last
65 s (mean 141-159, min 44-75, max 237-255, contrast 13-18, 100% non-black).
Totals: 3572 units received, 243 dropped (26 early + 217 at the IDR re-sync
during the transition, stable afterwards), 3329 decoded, 5024 audio packets,
6480 render submissions; received minus dropped equals decoded exactly. This
is content-level evidence that the displayed picture is real non-black game
imagery; it is not an eyeball or panel-photo confirmation, and the server
still reports gamepad disabled.

## RTC diagnostics and DTLS retransmission 0.2.11

The DTLS timer callback is now registered once after SSL setup, not on every
nonblocking handshake poll. Re-registering it cancelled the retransmission
timer. A real Mbed TLS/libsrtp regression drops the first client datagram and
advances a deterministic test-only clock: it failed before this fix and passes
after it. The normal handshake, SRTP roundtrip and mismatched-fingerprint
rejection still pass. No verification policy was weakened.

Numeric ICE diagnostics distinguish missing local/remote candidates, no pairs,
sent binding checks, validated/rejected replies, UDP receives and socket errors.
They are emitted at negotiation/state/error boundaries, including immediately
before timeout cleanup; there is no packet payload or endpoint logging. The
socket-poll loop no longer examines a disabled IPv6 slot. Five real transport/
polling host tests, RTC session policy, translations and safe-monitor tests pass.

Real CLI/GDB testing isolated the emulator timeout: local candidates=0, remote=1,
pairs=0 and binding checks sent=0. The private yuzu configuration had an empty
network_interface. Selecting the host's active WLAN restored a local candidate
and a validated binding reply. The ordinary user qt-config.ini was not changed.

The next real run identified a second cause: the fixed libpeer answer disagreed
with the offer's media order, MIDs, transport and formats (numeric mask=30).
The CloudGenshin-only answer builder now preserves offered order/MIDs and BUNDLE,
selects receive-only H264/Opus and SCTP, and uses only the locally generated ICE
credentials, DTLS role and fingerprint. Negotiated payload IDs configure RTP
receive demultiplexing; unrelated GFN behavior stays unchanged. Unsupported
offers and missing local candidates fail explicitly, rather than empty waiting.
The legacy parser receives bounded fields; raw SDP is not saved or exported.

After this change, a real saved-login/normal-queue run completed DTLS (sent=3,
received=3), with final answer mask=0. A bounded 60-second stream received and
decoded 2034 video units/frames, received 2462 audio packets and submitted 3441
render operations. Video drops/decode errors were zero. It ended at 73s with
TestEndedNotGameplayProof. These are real NRO counters, not mocks, but no UI
automation/screenshots, audible output or physical Switch display were checked.
The server reported gamepad disabled; controller delivery remains unresolved.

The user requested immediate debugging and no schedule; the heartbeat is paused.
The final hardened NRO was separately retested: the same saved-login/queue/
DTLS chain succeeded, with 2031 video units, 2005 decoded frames, 2438 audio
packets and 3371 render submissions. It dropped 26 video units near startup,
then the drop count stayed fixed; decode errors remained zero. It ended normally
at 75s. This exact paired NRO/ELF is packaged as alpha 0.2.11; physical-device
display/audio/input acceptance remains outstanding.

## Command-line NRO debugging and one-shot auto-queue 0.2.10

The native app has an exact opt-in marker for a single normal-queue attempt after
saved-login validation or a completed user QR confirmation. No OCR or desktop UI
automation is used. Missing marker keeps manual launch behavior. Login/queue/
stream deadlines, cancellation and a stop-file path bound the attempt; no paid
queue, purchase or repeat dispatch is added. The separate host monitor reads
allowlisted safe events and fixed GDB markers only, retains the first failure,
discards raw process stdout/stderr, and stops its own process after bounded cleanup.

Real local yuzu EA4176 + GDB 14.1 testing now reaches main, restores the supplied
encrypted login (LOGIN_RESTORED HTTP 200 without QR), automatically queues at
position 1, dispatches at 14s, gets signaling Upgrade 101 and game auth at 15s.
RTC stays CHECKING and reports RTC_CONNECT_TIMEOUT at 45s; every media counter is
zero. This emulator result differs from the real Switch's DTLS sent=1/received=0.
The test stops without claiming gameplay or acknowledged server cleanup.

Startup initially waited in a libnx system-service request before main. Native
builds now use an app-owned platform wrapper that omits the unused INS service;
required FS, socket, font, NIFM and power init behavior remains intact. Removing
that request was followed by a real main breakpoint hit. No vendored source was
modified for this startup fix; GFN keeps its inherited platform wrapper.

CLOUDGENSHIN_DEBUG_SYMBOLS preserves source symbols in the matching ELF. The
verified yuzu NRO text base is 0x80000000 and stub port is 6543. The debugger is
continued automatically, rather than leaving the guest parked at entry. This
stub does not support detach. Details, private runtime layout and bounded CLI
workflow are in cloudgenshin-headless-debug.md. No real account goes into fixtures
or deliverables. A first login network timeout was followed by successful real
validation; credentials were preserved and no new QR was required.

Remaining work includes emulator ICE/UDP interoperability, real-device DTLS
retransmission and actual media/gameplay. In particular, dtls_srtp_do_handshake
still registers its timer callback on every poll; resetting that callback can
clear the timer. Do not claim that retransmission issue was fixed in 0.2.10.

## Remember QR login 0.2.9

At the user's explicit request, successful QR authorization now saves the
minimum Combo/account/device identity under AppHomePath. On startup the native
view loads it and calls the existing official gamer-login verifier. A successful
verification restores the session without fetching or displaying a QR code.
Missing login asks for an explicit sign-in; rejected login asks for sign-in again.
Transport/HTTP/invalid-response failures keep the saved file for retry and never
automatically turn into a QR request. This does not bypass expiry or official
verification, and no token-refresh API is claimed.

The separate login_store module uses a bounded, versioned AES-256-GCM envelope,
a random 32-byte local key and a fresh 12-byte nonce per save. Vault and key files
are cloudgenshin-login.vault and cloudgenshin-login.key. They are written through
temporary files and the existing FAT-compatible replacement helper; loading tries
authenticated primary/backup combinations. Logout cancels/joins the worker,
clears RAM authorization and removes login files, with a durable disabled marker
preventing leftover backups from being loaded. A later successful save removes
that marker. Save or clear failure is visible rather than pretending persistence
succeeded. QR tickets, account-exchange temporary tokens, game credentials, SDP
and endpoints are not persisted by this feature.

The encryption key resides on the same SD card, so this is local encrypted storage,
not hardware-bound protection against someone who can read the whole card.
Treat both files and their backups as credentials; never share them with logs.
Safe events contain fixed status identifiers and numeric errors only. The existing
Chinese/English toggle also covers restore/save/logout messages.

Host checks use synthetic accounts only: roundtrip, changing ciphertext nonce,
tamper rejection, key/vault backup recovery, failed writes, logout backup blocking,
validation success/rejection/network/cancellation, no QR calls during validation,
and translated status messages. The storage test links the real Mbed TLS crypto
archive from the tests/dtls_host MinGW build at
../../.opennow-msys-tmp/dtls-host-mingw/mbedtls/library/libmbedcrypto.a.
Build that host dependency first, then run scripts/test-mihoyo-host.ps1 with
mihoyo_login_store,mihoyo_launch_flow,mihoyo_ui_language.

This release changes login persistence only. The user's 0.2.8 hardware log still
shows DTLS waiting after one transmitted datagram and no received datagrams;
media counters remain zero. Video/gameplay and real-device restart restoration
have not been verified here. DTLS retransmission remains follow-up work.

## DTLS handshake correction 0.2.8

The user's real 0.2.7 log reaches PEER_CONNECTION_CONNECTED (3), then returns to
CHECKING and FAILED around the library's 3.5-second DTLS endpoint timeout. Media
counters remain zero. This establishes progress beyond previous UDP candidate
acceptance, but does not reveal the actual TLS error or peer SDP setup value.

Necessary libpeer patches now honor the parsed remote setup role when creating
an answer, preserve nonblocking server handshake progress across polls, use a
per-DTLS-object retransmission timer, configure TLS defaults before explicit
authentication settings, and check RNG/key/certificate/TLS initialization results.
The native Cloud build selects ECDSA P-256; Switch DTLS explicitly registers
libnx randomGet as a strong entropy source because the vendored Mbed TLS config
disables platform entropy. Only AES128_CM_HMAC_SHA1_80 is advertised, matching
the existing SRTP policy. Exact remote fingerprint comparison remains required;
mismatch/missing certificates now return recognizable numeric errors without
printing fingerprints. Normal HTTP/WSS TLS certificate verification is unchanged.
The role mapping follows the setup attribute rules in
[RFC 5763](https://www.rfc-editor.org/rfc/rfc5763.html#section-5).

Safe transport snapshots expose only local DTLS role, numeric last result and
sent/received handshake-datagram counts at answer creation and transport state
boundaries. Init failures are included. No raw SDP, keys, endpoints or fingerprint
values enter these events. Native same-transport renegotiation is handled since
0.2.49 (see that section); ICE-restart renegotiation, ICE-TCP and
controller-disabled service responses remain separate unsupported/unresolved
behavior.

`tests/dtls_host` is a standalone host CMake project using the real bundled Mbed
TLS/libsrtp over bounded in-memory datagrams. Its entropy-init wrapper supplies
host OS randomness only in the test object; production uses Switch randomGet.
It checks completed DTLS, usable SRTP encryption/decryption and rejection of a
deliberately wrong fingerprint. Three additional peer tests cover active/passive/
actpass answer selection, nonblocking receive and completed DTLS receive loops.
These do not use an account or official server and do not prove device gameplay.

## Chinese interface and IPv4 RTC routing 0.2.7

Native launcher and stream diagnostics now default to simplified Chinese, with
an immediate Chinese/English toggle and an atomically saved two-letter language
preference under AppHomePath. Returning from a stream refreshes launcher labels
if the language changed there. Chinese system/custom-font fallback is explicitly
registered by the native view. Displayed safe-log entries translate event names,
common RTC state/category codes and numeric field labels; unknown events retain
their identifiers. The protocol and original safe log retain fixed English tags.
Manual export also writes cloudgenshin-safe-zh.log through the existing atomic
safe-log writer; the formatter only sees the already-sanitized snapshot and uses
an explicit language, without reading UI preferences from a background worker.

The user's 0.2.6 hardware log confirms UDP candidate acceptance and TCP candidate
skip, followed by PEER_CONNECTION_FAILED (5) with zero media packets. Static public
cg-sdk 6.1.0.62 WebSessionInfo pairs dispatch WSS/address lists and supplies
sdpCandidateList using getServerIpAddr() and rtcPort; onSignalingMessage replaces
the candidate IP/port before addIceCandidate. The native path omitted this
replacement. It now pairs primary/alternate IPv4 dispatch routes with the actual
successfully connected normalized signaling endpoint, and replaces only a valid
UDP candidate's address/port. Unpaired/invalid/zero-port routes keep the original
candidate; no arbitrary address or network route is guessed. IPv6 override and
ICE-TCP remain unimplemented. No endpoint/SDP is persisted or exported.

Regression coverage includes route preservation, primary/alternate matching,
language persistence and backup recovery, Chinese formatting and unchanged raw
safe events. Real RTC/media/render success remains unverified. Remember-login is
still pending and is not part of this release.

## ICE candidate handling 0.2.6

Real 0.2.5 hardware reached Upgrade 101, successful game authorization (Xbox
disabled), and an RTC answer, then stopped immediately on candidate rejection.
The exact rejected candidate category was not present in that safe log.
Native Session previously treated every rejected candidate as session-fatal;
the static official SDK catches per-candidate errors without closing the peer.
0.2.6 keeps processing other candidates and retains the connection deadline.
Candidate input is bounded/classified before libpeer's legacy sscanf parser;
duplicates are suppressed, and at most 64 distinct trickled candidates are
processed per session. Safe callbacks expose only fixed categories and results.
No new ICE-TCP support or public-address rewriting is claimed. Real media and
controller support remain unverified. Login persistence and bilingual UI are
requested follow-up work and are not implemented in this release.

## Game signaling route correction 0.2.5 (2026-10-13)

The user's 0.2.4 hardware log reaches dispatch and receives an HTTP 233 response
to the game WebSocket Upgrade in 249 ms. There are still zero video/audio units.
The status does not identify an official business/risk error; do not interpret
233 as successful WebSocket Upgrade or bypass the required HTTP 101 validation.

Static public cg-sdk.c39c60f7.js (SDK 6.1.0.62) contains
`WebSessionInfo.getServerWssUrl()`, which adds `/rtc-sdk` to the dispatched base
URL before passing it to the signaling WebSocket. Our native SignalingClient
omitted that step. Version 0.2.5 applies this route to primary and alternate
game endpoints, retaining query bytes, explicit ports and IPv6 brackets, and
avoiding duplicate suffixes/trailing separators. The shared WebSocket transport,
ping URLs and control-channel URLs are not changed. Endpoint deduplication occurs
after normalization. No endpoint or query enters logs.

Regression coverage checks actual outgoing game handshake targets, existing
suffixes, queries, IPv6, alternate routes and unchanged shared ping paths.
Real hardware must still establish Upgrade 101, game authentication, RTC, media
reception, decoding and visible rendering. The missing route is a confirmed
implementation discrepancy; whether it explains this hardware HTTP 233 response
remains unverified until retest.

## Game WSS revision 0.2.4 (2026-10-12)

The supplied 0.2.3 safe log now establishes all of QR/Combo/cloud LOGIN_OK,
five successful WSS RTT targets, accepted measured-node request, node verification,
normal queue position 2, and DISPATCH_READY_FOR_GAME_AUTH. The stream's failure
is immediate SIGNALING_CONNECT_FAILED at t=40s; all video/audio/decode/render
counters remain zero. VIDEO_DECODER_SOFTWARE_READY establishes initialization
fallback, not decoded video. The old generic tag does not establish whether the
game endpoint failed at DNS/TCP/TLS or HTTP Upgrade; Origin is not a proven cause.

- WebSocket connect exposes fixed stage tags plus actual numeric curl result,
  numeric Upgrade HTTP status, and elapsed milliseconds. No URI/address, query,
  headers, reason phrase, bodies, cookies, QR content or account/session data
  enter these diagnostics. A failed valid-101 check remains a hard failure.
- URL parsing now separates query-only request targets from Host (previously
  `wss://host?query` generated a Host containing query and lost the GET query).
  Bracketed IPv6/explicit ports and path queries are preserved; credentials,
  fragments, whitespace/control characters and unbracketed IPv6 are rejected.
- The existing public web SDK uses browser WebSocket, whose page Origin is sent
  automatically. Cloud-only signaling now supplies Origin https://ys.mihoyo.com
  and an honest native User-Agent. This is a compatibility correction, not proof
  that the observed failure was an Origin rejection; no browser cookie is copied.
- Dispatch's additional WSS addresses (and IPv6 variants) are retained in RAM
  and passed to the connection owner. Up to four unique validated secure addresses
  share a 20-second budget, at most six seconds per attempt. Only pre-game-auth
  transport/read/write/timeout failures or HTTP 502/503/504 can try an alternative.
  Permission/risk/rate-limit responses, invalid 101 proofs, cancellation, insecure
  URLs and malformed URLs do not trigger permission-bypassing retries. No address
  is invented, printed or persisted, and every attempt verifies TLS normally.
- Only the successfully established socket gets StartGameReq. Official game
  authorization still precedes RTC hello/peer initialization. No account/profile
  switching or paid-queue fallback is introduced.

Targeted fixtures exercise TCP/DNS/certificate result propagation, HTTP 403/503,
Origin/Host/query requests, malformed Upgrade rejection, service-list fallback,
game-auth ordering after fallback, and continued ping/launch behavior. The account's
real game WSS acceptance and gameplay remain hardware validation gates.

## First-frame / diagnostics revision 0.2.3 (2026-10-12)

The user reports that 0.2.2 now passes ping and queue and opens a black stream
screen; the hold +/- return shortcut did not respond and the launcher-only
export could not be used. This is user-reported progress, not an observed live
RTC/decoded-frame result. The pasted 0.2.1 file can be an old manual export;
the PC cannot read the Switch's memory or SD remotely without a transfer.

This revision retains the real login/dispatch/WSS/WebRTC/H.264 path and changes
the native stream owner, not the service protocol based on an unobserved guess:

- Explicit full-window view dimensions, plus an in-view diagnostic panel with
  controller/touch Export, Stop + return and Hide buttons. Minus opens the panel;
  unified/local-any-controller +/- hold is tested at 800ms. Diagnostics do not
  open a modal activity (which would cancel streaming via willDisappear).
- Cancel first, wait for the owned network worker, then pop and restore launcher
  focus. Panel input is neutralized instead of sending UI clicks to the game.
- Network completion requests decoder stop but no longer frees AVFrameHolder's
  UI-borrowed frame. Final renderer/decoder/frame cleanup remains on the UI owner
  after joining the network worker; bounded decode queues/resync are retained.
- Absent proto3 FPS (zero) defaults to 30, rather than silently selecting 1 FPS.
- Fixed safe tags distinguish game authorization, WSS, SDP answer, peer state,
  first video/audio callback, first decoded frame and render submission. Numeric
  snapshots record video units/IDRs/decoded/dropped/errors, last decode error,
  audio packets and render submissions every five seconds, never per packet.
  Every five seconds a sparse luma grid of the frame handed to the renderer also
  reports brightness mean/min/max, contrast and non-black share, so a black
  screen can be told apart from real imagery without exporting pixels.
  The automated stream window is 110 s so a slow queue still leaves time for
  the cloud client startup page to turn into real game video.
  First borrowed frame dimensions/pixel format and failed render view dimensions
  help localize a decoder-vs-renderer mismatch. No packet bytes/SDP are exported.
- The safety logger keeps at most 512 events in RAM; current-version/run-time
  headers and atomic SD saves occur at entry/login/stream entry, snapshots and
  failures. Manual export is available inside a black stream. SD failure keeps
  the RAM log and previous complete file usable; no credentials are persisted.

Host tests cover safe-event bounds/concurrent append/atomic write/failure recovery,
FPS defaults, hold release/reset/threshold and fixed RTC failure tags, alongside
the existing login/dispatch/RTC regressions. These are policy/transport-fixture
tests, NOT Switch GPU rendering or live cloud account tests. A cross-built NRO
is necessary but does not establish successful video or controller delivery.
Next hardware acceptance is: real Start -> observe decoded/submitted counters
and actual moving image, or supply this build's safe SD log / panel photo.

## Node revision 0.2.2 (2026-10-12)

Real Switch logs now establish QR scanned/Account confirmed, successful Combo
exchange, CLOUD_ACCOUNT_AUTH and LOGIN_OK. Service/ping-list requests pass;
getNodesInfo rejects with -110013. No dispatch, RTC or gameplay is proven.
The old implementation discarded sdk_param and submitted empty node without
ever running speed testing. Its fallback-only path failed on this account;
the error code alone does not prove which backend validation rejected it.

The current official public web entry web.b959658b.js still sends numeric
speed_client_type=7 for desktop/web keyboard, so this revision does not blindly
change it to Android's string representation. listPingServer ext_data now
contains the observed platform=19. Newly invoked native work:

- Retain ping SDK metadata in RAM; bounded Base64/protobuf decode of
  msg.ListPingSvrRsp / common.PingSvrInfo, including WSS URLs and server IDs.
- Official SDK WebSocketRttTester protocol: twenty TEXT X sends, FIFO replies,
  rounded mean RTT. TLS verification stays on. No game hello/auth is sent to
  echo endpoints and no RTT is invented on timeout or incomplete replies.
- Up to 32 service-returned targets, four workers, 2.5s per target, 12s overall,
  cancellation/join. Only complete measurements enter node.ping_results;
  bandwidth=0 explicitly means the SDK's skip-bandwidth mode, not a measured
  throughput. Full bandwidth/loss/jitter benchmarking is not implemented.
- getNodesInfo receives the serialized measured node object, not empty node.
  Invalid metadata, all failed probes or cancellation stop before this call.
  Safe logs show counts/stages only, never addresses or SDK metadata.

Host fixtures exercise metadata bounds, the real probe implementation against
fake WSS echoes, twenty X messages, timeout/cancel/insecure URL rejection and
no downstream dispatch on probe failure. The anonymous ping-list check returns
-100 without authorization, so live probing cannot be tested from this PC
without a user-authorized session; no tokens were requested/copied. Switch
RTT/node acceptance and actual gameplay still require testing the new NRO.

## QR revision 0.2.1 (2026-10-12)

A real Switch 0.2.0 report shows QR fetch HTTP 200, then query HTTP 200 with
retcode -3503, then SIGN_IN_REQUIRED. This establishes successful HTTPS/QR
fetch, NOT successful native account authorization, dispatch or video. The
phone scanner was MiYouShe; the response payload/protocol was not received.
The old log contains no server message or timing, so its exact rejection
reason cannot be established from that log alone.

- Fetch/query now consistently send the generated device ID in both body
  `device` and header `x-rpc-device_id`, with the same fixed app/game profile.
  This fixes an omitted standard SDK identity header; it is **not proof that
  -3503 will disappear**, and not device-fingerprint emulation.
- Confirmed Account payloads now go through the official signed
  `/combo/granter/login/v2/login` endpoint before cloud authorization. Combo
  payloads continue directly. Unknown protocols stay rejected. This addresses
  a separate compatibility gap, not the already observed -3503 itself.
- Risk, official verification-required, expiration, phone cancellation,
  transport, HTTP and malformed-response states are distinguished. A fixed
  risk message can map -3503 to QR_OFFICIAL_RISK_REJECTED; other messages are
  not retained or emitted. Failures log elapsed seconds and poll count once.
- Optional header probing reports only whether a nonempty `x-rpc-aigis`
  response header exists. It never retains the header/challenge content or
  cookies. No automated retry of an official risk rejection or account/app ID
  rotation is implemented. Start without authorization no longer hides the
  previous failure as SIGN_IN_REQUIRED.
- **No native slider/CAPTCHA page or phone/SMS login UI is implemented.** The
  supplied APK's CommonResponse maps -3101 to Aigis verification; LoginManager
  reads x-rpc-aigis, invokes JiyanUtils/GT3 or GTCaptcha4, then retries with the
  user-completed result. A generic -3503 is not that challenge contract. Web
  applet/JS compatibility, challenge presentation and result handoff require
  separate implementation and real-device verification.

Anonymous production fetch/query with the revised headers returned 0 / Init,
with no scan or saved QR material. This does not reproduce the post-scan
rejection or validate Account exchange with a real account. Synthetic tests
exercise both payload paths, signed exchange golden bytes, rejection and
cancellation gates, challenge presence detection and no cloud login on failure.

The reported -3503 risk text is consistent with a first-hand public report:
https://github.com/UIGF-org/mihoyo-api-collect/discussions/72 . Treat this as
reported behavior, not an official guarantee of this user's exact cause.

## Original integration baseline (2026-10-11)

This is a native integration **test build, not a finished/playable release**.
The CMake option CLOUDGENSHIN_NATIVE=ON now boots the actual Cloud Genshin
screen, not the inherited GeForce NOW launcher. The original GFN launcher is
retained behind CLOUDGENSHIN_NATIVE=OFF; output and data paths remain
SwitchNOW.nro and sdmc:/switch/SwitchNOW/ for compatibility.

## Implemented and invoked from the NRO

- Official QR fetch/query, using the same generated device ID in both requests.
  QR is rendered only on the user's device and never saved or logged. Polling,
  phone-confirmation state, expiry, cancellation and retry are wired to the UI.
- Confirmed Combo authorization is kept in RAM while in use, and 0.2.9 adds the
  user-authorized encrypted SD login store described above. Gamer login uses the Combo SDK
  public application key and hk4e_cn business identity. Dispatcher signatures
  use the separate public cloud key. Neither key is a user's credential. An
  explicit passport aid/uid is preferred. For the official CN app_id=4,
  channel_id=1 profile only, numeric open_id follows the APK's aid mapping
  (SdkLoginManager account whitelist check). Opaque/other-channel IDs are not
  guessed; missing account ID blocks allocation with ACCOUNT_ID_REQUIRED_NO_DISPATCH.
- Service status, ping-server discovery, server-recommended node selection,
  signed pre-dispatch verification, normal-queue dispatch, polling, ACK and
  best-effort leave on cancellation/error. The node path is the official
  empty-ping-node fallback, **not a measured RTT or bandwidth result**.
  No automatic paid/coin-queue switching or purchasing.
- Actual game authentication over signaling proxy: StartGameReq 20001 ->
  StartGameRsp 20002 (success is **1**, with config and nonzero game_app_id) ->
  RTC client hello -> offer/answer/candidates. Server-created binary data
  channel carries XInput-compatible gamepad packets and command-20029
  heartbeat. WebSocket keepalive is empty **text**.
- Dedicated media/input view, not GFN WebRtcSession with a substituted URL:
  H.264 decoder with NVDEC initialization/software fallback, bounded
  access-unit queue, IDR resynchronization/keyframe requests, existing Deko3D
  renderer and Opus pipeline. Physical Switch button positions map to Xbox
  controls. Hold Plus and Minus for 0.8 seconds to stop/pop the stream view.
- RAM event log visible without SD access. Manual safe SD export includes only
  fixed events and numeric HTTP/API/queue information. No cookies, QR data,
  account IDs, tokens, SDP, endpoints or response bodies are exported.
  HTTP/WebSocket connect can be cancelled; queue cleanup is bounded to five
  seconds. Cloud boot does not delete/authenticate unrelated GFN credentials.
- Standard Mozilla CA bundle is embedded for verified HTTPS/WSS, rather than
  relying only on the inherited NVIDIA supplemental root. Native alpha NACP
  and on-screen version now both identify 0.2.10; GFN version remains unchanged.

## Evidence versus outstanding real-device work

- Official production anonymous QR fetch returned retcode 0. Query using the
  same app_id/device/ticket returned retcode 0, state Init. Both responses were
  passed in memory to the new C++ parser, which passed. No account was scanned
  by the agent; no challenge/response material was retained.
- Synthetic host tests verify signed requests, dispatch decoding, QR parsing,
  malformed bounds, control golden bytes, authentication ordering, queue
  cancellation/ACK failure cleanup, no allocation without account ID, binary
  signaling and empty-text keepalive. Host test doubles do not enter the NRO.
- Switch devkitPro cross-build links the actual launcher, HTTP flow, RTC
  session and media view. This proves compilation/linking, not gameplay.

User-supplied 0.2.3 safe logs establish phone-confirmed native account/Combo
exchange, cloud LOGIN_OK, all ping probes, accepted nodes, verification, normal
queue and dispatch-to-stream handoff. The actual game WSS connection then fails;
no complete video unit has been received.

**Not yet verified:** live SDP/ICE interoperability, received/decoded cloud video,
game picture, sound and controller delivery on a real Switch. A successful
send counter is not proof of server delivery or game response.

**Not implemented as product features:** separate WebSocket/WebTransport game
control-channel negotiation (RTC fallback only), server-directed ICE-restart
renegotiation (same-transport media updates are answered since 0.2.49),
automatic reconnect, mid-session signaling endpoint failover, credential
refresh, billing/agreement/user-center UI, full network benchmarking,
extended-game messages (text entry etc.). Multi-game profiles for 云·星穹铁道 and
云·绝区零 are implemented (see the 0.2.11 multi-game section); 云·绝区零 login
returned -100002 under the shared H5 identity, and 0.2.12 mirrors the official
Android app's cloud request identity instead (see the 0.2.12 section); the
real-device round is pending. Allocations
whose responses are lost or cannot be handed off can still depend on server
expiry; best-effort stop is not an acknowledged resource-release guarantee.
No stability or completion claim is made for these.

## Next validation gate

Run the alpha in full-memory title-override mode (not Album applet mode).
For 0.2.9, sign in once, confirm LOGIN_SAVED, exit and reopen the NRO. Verify
LOGIN_RESTORED with no QR request before starting the normal queue. If no saved
login exists, choose Sign in, scan and confirm on the phone. Provide the safe
log or a photo **after the QR disappears** if an operation fails. Do not send
a photo containing a live QR, token or account-information screen.
For a black stream, use its in-view Export button or photograph the safe panel;
the current log also autosaves without returning to the launcher.

Completion requires a real Switch completing native login and queue, then
displaying the actual game, playing sound and responding to Joy-Con input.
Until this is observed the build remains an integration alpha.

Windows host tests: ./scripts/test-mihoyo-host.ps1. The shared streaming-test
runner also includes pure protocol tests. APK schemas and static evidence
are in cloudgenshin-android-apk-findings.md.

## 2026-10-21 handoff (0.2.42-d / 0.2.43)

The migration handoff is `docs/cloudgenshin-handoff-2026-10-21.md`; read it
before changing login or input code. It records the uncommitted state, the
0.2.43 real-device evidence, the confirmed official login contract, and the
35-4301 investigation plan.

- 0.2.42-d: normalized `RtcMouse.cursorX/cursorY` in [0,1]; the entry click at
  (0.5, 0.947) reaches the game, but a system dialog appears about two seconds
  later.
- 0.2.43: CPS/UAPC now return the official values (`pc_mihoyo` / empty) and
  the replies grew to 74 B / 65 B on the wire. The dialog is unchanged:
  **error code 35-4301**. Frame dumps 6-11 show the same static
  "系统提示 / 错误码: 35-4301 / 确认" dialog. CPS is not the cause.
- Strongest current hypothesis: the saved Panda Combo QR login has no
  passport `stoken`, so `login_login` sends an empty `channel_token`; the
  bridge also omits the official `combo_id`, `account_type`, `heartbeat`,
  `asterisk_name` and flattened `data` fields. The official web result sets
  `channel_token` from the passport `stoken` and fetches the Combo result via
  `comboWebLogin`.
- Next gate: probe the passport web QR endpoints, implement passport QR ->
  `comboWebLogin` -> complete login fields (0.2.44), then re-run. Do not
  disable TLS, print vault/key/token values, auto-purchase, switch queues,
  bypass captchas, or end a session with Esc.
