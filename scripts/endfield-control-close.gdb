# Read-only, scalar-only diagnostics. Use ONLY with the matching ELF/NRO and
# isolated yuzu EA4176 config, text base 0x80000000, localhost port 6543.
# No buffers, auth values, raw protocol, endpoints or account fields are read.
set pagination off
set confirm off
set print frame-arguments none
set print entry-values no
set tcp auto-retry on
set tcp connect-timeout 45
set remotetimeout 5
target remote 127.0.0.1:6543
printf "GDB_TARGET_CONNECTED\n"
break cloudgenshin::endfield::VlinkSession::transport_stats
commands
  silent
  # The verified function-entry breakpoint precedes any change to AArch64 x0.
  # devkitPro's bundled GDB has no Python support; use native scalar reads.
  set $view = (cloudgenshin::endfield::VlinkSession*)$x0
  if $view && $view->peer_
    set $peer = $view->peer_
    set $queued = 0
    if $peer->sctp.transport
      set $queued = $peer->sctp.transport->output_count
    end
    printf "GDB_CONTROL_STATE connected=%d rx_dtls=%u rx_sctp=%u queued=%u failed=%d\n", $peer->sctp.connected, $peer->completed_dtls_packets, $peer->sctp_read_events, $queued, $peer->sctp.receive_failed
  end
  continue
end
# Periodic stop/read/resume can disturb the real-time stream in this emulator.
# Leave that diagnostic disabled unless deliberately enabled by an operator.
disable 1
break cloudgenshin::endfield::VlinkSession::OnControlClosed
commands
  silent
  set $view = (cloudgenshin::endfield::VlinkSession*)$x0
  if $view && !$view->closing_ && $view->peer_
    printf "GDB_CONTROL_CLOSE receive_failed=%d connected=%d message_bytes=%u\n", $view->peer_->sctp.receive_failed, $view->peer_->sctp.connected, $view->peer_->sctp.message_len
  end
  continue
end
continue
