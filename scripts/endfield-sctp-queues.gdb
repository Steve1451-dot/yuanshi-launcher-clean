# Read-only, integer-only diagnostics; matching ELF/NRO required.
# yuzu EA4176 base 0x80000000, loopback port 6543. No payloads/identities.
set pagination off
set confirm off
set print frame-arguments none
set print entry-values no
set tcp auto-retry on
set tcp connect-timeout 45
set remotetimeout 5
target remote 127.0.0.1:6543
printf "GDB_TARGET_CONNECTED\n"
# Two snapshots around 50/100 s; then remove the periodic breakpoint.
set $hits = 0
break cloudgenshin::endfield::VlinkSession::transport_stats
commands
  silent
  set $hits = $hits + 1
  if $hits == 11 || $hits == 21
  set $view = (cloudgenshin::endfield::VlinkSession*)$x0
  if $view && $view->peer_ && $view->peer_->sctp.sock
    set $peer = $view->peer_
    set $sock = $peer->sctp.sock
    set $endpoint = (struct sctp_inpcb*)$sock->so_pcb
    if $endpoint && $endpoint->sctp_asoc_list.lh_first
      set $assoc = &$endpoint->sctp_asoc_list.lh_first->asoc
      printf "GDB_SCTP_QUEUES sent_bytes=%u recv_bytes=%u reasm=%u ordered=%u unacked=%u retry=%u peer_window=%u local_window=%u partial=%u failed=%d\n", $sock->so_snd.sb_cc, $sock->so_rcv.sb_cc, $assoc->size_on_reasm_queue, $assoc->cnt_on_all_streams, $assoc->sent_queue_cnt, $assoc->sent_queue_retran_cnt, $assoc->peers_rwnd, $assoc->my_rwnd, $peer->sctp.message_len, $peer->sctp.receive_failed
      printf "GDB_CONTROL_STATE connected=%d rx_dtls=%u rx_sctp=%u queued=%u failed=%d\n", $peer->sctp.connected, $peer->completed_dtls_packets, $peer->sctp_read_events, $peer->sctp.transport->output_count, $peer->sctp.receive_failed
      # Sequence gaps/fragment flags only: no raw TSN, packet/body or IDs.
      if $assoc->streamincnt > 0 && $assoc->strmin[0].inqueue.tqh_first
        set $stream = &$assoc->strmin[0]
        set $head = $stream->inqueue.tqh_first
        set $gap = (unsigned int)$head->mid - (unsigned int)$stream->last_mid_delivered - 1
        if !$assoc->idata_supported
          set $gap = $gap & 65535
        end
        printf "GDB_SCTP_STREAM sid=0 gap=%u first=%d last=%d end=%d partial=%d bytes=%u idata=%d\n", $gap, $head->first_frag_seen, $head->last_frag_seen, $head->end_added, $stream->pd_api_started, $head->length, $assoc->idata_supported
      end
      if $assoc->streamincnt > 1 && $assoc->strmin[1].inqueue.tqh_first
        set $stream = &$assoc->strmin[1]
        set $head = $stream->inqueue.tqh_first
        set $gap = (unsigned int)$head->mid - (unsigned int)$stream->last_mid_delivered - 1
        if !$assoc->idata_supported
          set $gap = $gap & 65535
        end
        printf "GDB_SCTP_STREAM sid=1 gap=%u first=%d last=%d end=%d partial=%d bytes=%u idata=%d\n", $gap, $head->first_frag_seen, $head->last_frag_seen, $head->end_added, $stream->pd_api_started, $head->length, $assoc->idata_supported
      end
    else
      printf "GDB_CONTROL_DIAG_UNAVAILABLE code=1\n"
    end
  end
  end
  if $hits >= 21
    disable 1
  end
  continue
end
# Before local destruction resets the receive-error flag.
break *sctp_destroy_association
commands
  silent
  set $association = (Sctp*)$x0
  if $association && $association->receive_failed
    printf "GDB_CONTROL_CLOSE receive_failed=%d connected=%d message_bytes=%u\n", $association->receive_failed, $association->connected, $association->message_len
  end
  continue
end
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
