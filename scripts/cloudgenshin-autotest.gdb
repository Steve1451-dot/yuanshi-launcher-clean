# For the verified yuzu EA4176 NRO text base (0x80000000) and port 6543.
# The launcher supplies the matching symbol file before sourcing this file.
set pagination off
set confirm off
set print frame-arguments none
set print entry-values no
set tcp auto-retry on
set tcp connect-timeout 60
set remotetimeout 5
target remote 127.0.0.1:6543
printf "GDB_TARGET_CONNECTED\n"
tbreak main
commands
  silent
  printf "GDB_MAIN_REACHED\n"
  continue
end
tbreak cloudgenshin::ResumeLogin
commands
  silent
  printf "GDB_LOGIN_VALIDATION_REACHED\n"
  continue
end
tbreak cloudgenshin::Launch
commands
  silent
  printf "GDB_QUEUE_FLOW_REACHED\n"
  continue
end
break http_client.cpp:183
commands
  silent
  if result != 0
    printf "GDB_HTTP_CURL_CODE code=%d\n", result
  end
  continue
end
break svcBreak
commands
  silent
  printf "GDB_SVC_BREAK reason=%u\n", $w0
end
continue
