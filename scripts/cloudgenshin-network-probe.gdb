# Safe call-site probe only: never inspect exception text, HTTP requests,
# headers, account objects, memory buffers or credentials.
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
break __cxa_throw
commands
  silent
  up 1
  printf "GDB_EXCEPTION_CALLER pc=0x%lx\n", $pc
  down 1
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
