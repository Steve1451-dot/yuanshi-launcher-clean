param(
    [Parameter(Mandatory)][string]$AppDirectory,
    [Parameter(Mandatory)][string]$StatusPath,
    [Parameter(Mandatory)][string]$GeetestScriptPath,
    [ValidateRange(1024,65535)][int]$Port=8769,
    [ValidateRange(5,60)][int]$LifetimeMinutes=20
)
$ErrorActionPreference='Stop'
# Development helper for the official HG device-check flow. OTPs, passport
# tokens, captcha answers and device proofs exist only in memory. The browser
# receives no account credential. The only persisted secret is the existing
# authenticated AES-GCM vault, upgraded atomically after successful checking.
$appRoot=[IO.Path]::GetFullPath($AppDirectory)
$keyPath=Join-Path $appRoot 'endfield-login.key'
$vaultPath=Join-Path $appRoot 'endfield-login.vault'
$key=[IO.File]::ReadAllBytes($keyPath)
$vault=[IO.File]::ReadAllBytes($vaultPath)
$plaintext=[byte[]]::new($vault.Length-36)
$aad=[Text.Encoding]::UTF8.GetBytes('CloudGenshin endfield store v1')
$aes=[Security.Cryptography.AesGcm]::new($key,16)
$client=$null;$listener=$null
$baseUrl="http://127.0.0.1:$Port"
$browserCookie=[guid]::NewGuid().ToString('N')
$deadline=[DateTimeOffset]::UtcNow.AddMinutes($LifetimeMinutes)
$state=[ordered]@{State='Ready';ApiStatus=0;Http=0;DeviceProofSaved=$false;
    GameAuthorizationReady=$false;LastAction='None';ProcessId=$PID;
    ExpiresAt=$deadline.ToString('o')}
$lastSms=[DateTimeOffset]::MinValue
$captchaChallenge='';$captchaAction=''

function Publish-State {
    [IO.File]::WriteAllText($StatusPath,($state|ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
}
function Send-Bytes($context,[byte[]]$bytes,[string]$contentType,[int]$http=200) {
    $context.Response.StatusCode=$http
    $context.Response.ContentType=$contentType
    $context.Response.Headers['Cache-Control']='no-store'
    $context.Response.Headers['X-Content-Type-Options']='nosniff'
    $context.Response.Headers['Referrer-Policy']='no-referrer'
    $context.Response.ContentLength64=$bytes.Length
    $context.Response.OutputStream.Write($bytes,0,$bytes.Length)
    $context.Response.Close()
}
function Send-Json($context,$body,[int]$http=200) {
    Send-Bytes $context ([Text.Encoding]::UTF8.GetBytes(($body|ConvertTo-Json -Depth 6 -Compress))) 'application/json; charset=utf-8' $http
}
function Invoke-Hg([string]$path,$body) {
    $content=[Net.Http.StringContent]::new(($body|ConvertTo-Json -Depth 6 -Compress),[Text.Encoding]::UTF8,'application/json')
    $response=$null
    try {
        $response=$client.PostAsync('https://as.hypergryph.com'+$path,$content).GetAwaiter().GetResult()
        $state.Http=[int]$response.StatusCode
        $text=$response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        if($text.Length -gt 65536){throw 'Oversized account response'}
        return $text|ConvertFrom-Json -AsHashtable -Depth 20
    } finally {if($response){$response.Dispose()};$content.Dispose()}
}
function Save-DeviceProof([string]$proof) {
    if([string]::IsNullOrWhiteSpace($proof) -or $proof.Length -gt 8192 -or $proof -match '[^\x21-\x7e]') {
        throw 'Invalid device proof'
    }
    $record.version=3;$record.device_token=$proof
    if(!$record.ContainsKey('uid')){$record.uid=''}
    $bytes=[Text.Encoding]::UTF8.GetBytes(($record|ConvertTo-Json -Compress -Depth 5))
    $nonce=[Security.Cryptography.RandomNumberGenerator]::GetBytes(12)
    $tag=[byte[]]::new(16);$cipher=[byte[]]::new($bytes.Length)
    $roundtrip=[byte[]]::new($bytes.Length)
    try {
        $aes.Encrypt($nonce,$bytes,$cipher,$tag,$aad)
        $aes.Decrypt($nonce,$cipher,$tag,$roundtrip,$aad)
        if(![Security.Cryptography.CryptographicOperations]::FixedTimeEquals($bytes,$roundtrip)) {
            throw 'Vault verification failed'
        }
        [byte[]]$encoded=[Text.Encoding]::ASCII.GetBytes('CEFG0001')+$nonce+$tag+$cipher
        if($encoded.Length -gt 16384){throw 'Vault exceeds limit'}
        $tempPath=$vaultPath+'.tmp'
        [IO.File]::WriteAllBytes($tempPath,$encoded)
        [IO.File]::Replace($tempPath,$vaultPath,$vaultPath+'.bak')
        $state.DeviceProofSaved=$true
    } finally {
        [Security.Cryptography.CryptographicOperations]::ZeroMemory($bytes)
        [Security.Cryptography.CryptographicOperations]::ZeroMemory($roundtrip)
    }
}
function Api-Result($reply,[string]$action) {
    $status=if($reply.ContainsKey('status')){[int]$reply.status}else{-12}
    $state.ApiStatus=$status;$state.LastAction=$action
    if($status -eq 1 -and $reply.data.captcha) {
        $challenge=$reply.data.captcha
        if($challenge.geetestId -is [string] -and $challenge.challenge -is [string]) {
            $script:captchaChallenge=$challenge.challenge;$script:captchaAction=$action
            $state.State='NeedsCaptcha';Publish-State
            return @{state=$state.State;status=$status;captcha=@{
                geetestId=$challenge.geetestId;riskType=$challenge.riskType;challenge=$challenge.challenge}}
        }
    }
    $state.State=if($status -eq 0){'Success'}elseif($status -eq 109){'NeedsDeviceVerification'}else{'OfficialRequestRejected'}
    return @{state=$state.State;status=$status}
}

try {
    if($key.Length -ne 32 -or $vault.Length -lt 37 -or [Text.Encoding]::ASCII.GetString($vault,0,8) -ne 'CEFG0001') {
        throw 'Invalid saved account format'
    }
    $aes.Decrypt([byte[]]$vault[8..19],[byte[]]$vault[36..($vault.Length-1)],
        [byte[]]$vault[20..35],$plaintext,$aad)
    $record=[Text.Encoding]::UTF8.GetString($plaintext)|ConvertFrom-Json -AsHashtable
    if(!$record.hg_token -or $record.hg_token.Length -gt 4096){throw 'Missing saved passport'}
    $handler=[Net.Http.HttpClientHandler]::new();$handler.UseProxy=$false
    $client=[Net.Http.HttpClient]::new($handler);$client.Timeout=[TimeSpan]::FromSeconds(15)
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36')
    foreach($pair in @(@('X-DeviceModel','Chrome'),@('X-DeviceType','7'),@('X-OSVer','Windows'),
        @('X-DeviceId',[guid]::NewGuid().ToString('N')),@('X-Captcha-Version','4.0'),
        @('Accept-Encoding','identity'),@('Connection','close'))) {
        $null=$client.DefaultRequestHeaders.TryAddWithoutValidation($pair[0],$pair[1])
    }
    $listener=[Net.HttpListener]::new();$listener.Prefixes.Add($baseUrl+'/');$listener.Start()
    $state.State='Ready';Publish-State
    # The UI does not send SMS on page load. Only its explicit button does so.
    while([DateTimeOffset]::UtcNow -lt $deadline) {
        $pending=$listener.GetContextAsync()
        while(!$pending.Wait(1000)) {if([DateTimeOffset]::UtcNow -ge $deadline){break}}
        if(!$pending.IsCompleted){break}
        $context=$pending.GetAwaiter().GetResult()
        try {
            $request=$context.Request
            if($request.Url.Authority -ne "127.0.0.1:$Port") {Send-Json $context @{state='Forbidden'} 403;continue}
            $path=$request.Url.AbsolutePath
            if($request.HttpMethod -eq 'GET' -and $path -eq '/') {
                $context.Response.Headers['Set-Cookie']="cgnx_verify=$browserCookie; HttpOnly; SameSite=Strict; Path=/"
                Send-Bytes $context ([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'endfield-device-verify.html'))) 'text/html; charset=utf-8'
                continue
            }
            if($request.HttpMethod -eq 'GET' -and $path -eq '/gt4.js') {
                Send-Bytes $context ([IO.File]::ReadAllBytes($GeetestScriptPath)) 'text/javascript; charset=utf-8';continue
            }
            if($request.HttpMethod -ne 'POST' -or $request.Headers['Origin'] -ne $baseUrl -or
               $request.Headers['X-CGNX-Verification'] -ne '1' -or
               $request.Cookies['cgnx_verify'].Value -ne $browserCookie -or
               $request.ContentLength64 -lt 0 -or $request.ContentLength64 -gt 8192 -or
               !$request.ContentType.StartsWith('application/json')) {
                Send-Json $context @{state='Forbidden'} 403;continue
            }
            $reader=[IO.StreamReader]::new($request.InputStream,[Text.Encoding]::UTF8)
            try {$submission=$reader.ReadToEnd()|ConvertFrom-Json -AsHashtable -Depth 10} finally {$reader.Dispose()}
            $captcha=$null
            if($submission.captcha) {
                if($captchaAction -ne $path -or $submission.captcha.challenge -ne $captchaChallenge) {
                    Send-Json $context @{state='InvalidCaptchaContext'} 400;continue
                }
                $captcha=$submission.captcha
            }
            switch($path) {
                '/send' {
                    if(([DateTimeOffset]::UtcNow-$lastSms).TotalSeconds -lt 60) {
                        Send-Json $context @{state='Cooldown';status=0};continue
                    }
                    $body=@{token=$record.hg_token;type=6}
                    if($captcha){$body.captcha=$captcha}
                    $reply=Invoke-Hg '/user/info/v1/send_phone_code' $body
                    $result=Api-Result $reply $path
                    if($result.status -eq 0) {$lastSms=[DateTimeOffset]::UtcNow;$state.State='SmsSent';$result.state='SmsSent'}
                    Publish-State;Send-Json $context $result
                }
                '/verify' {
                    if($submission.code -isnot [string] -or $submission.code -notmatch '^\d{6}$') {
                        Send-Json $context @{state='InvalidCode';status=-9} 400;continue
                    }
                    $body=@{token=$record.hg_token;code=$submission.code;appCode='dd7b852d5f1dd9da'}
                    if($captcha){$body.captcha=$captcha}
                    $reply=Invoke-Hg '/user/info/v1/device_token_by_phone' $body
                    $result=Api-Result $reply $path
                    if($result.status -eq 0 -and $reply.data.deviceToken) {
                        Save-DeviceProof $reply.data.deviceToken
                        $state.State='DeviceVerified';$result.state='DeviceVerified'
                    }
                    $submission.code='';$body.code='';Publish-State;Send-Json $context $result
                }
                '/authorize' {
                    if(!$state.DeviceProofSaved -and !$record.device_token) {
                        Send-Json $context @{state='NeedsDeviceVerification';status=109};continue
                    }
                    $body=@{token=$record.hg_token;appCode='dd7b852d5f1dd9da';type=0;deviceToken=$record.device_token}
                    if($captcha){$body.captcha=$captcha}
                    $reply=Invoke-Hg '/user/oauth2/v2/grant' $body
                    $result=Api-Result $reply $path
                    if($result.status -eq 0 -and $reply.data.code) {
                        $state.GameAuthorizationReady=$true;$state.State='Complete';$result.state='Complete'
                    }
                    Publish-State;Send-Json $context $result
                }
                '/close' {Send-Json $context @{state='Closed'};$deadline=[DateTimeOffset]::UtcNow}
                default {Send-Json $context @{state='NotFound'} 404}
            }
        } catch {
            # No exception message/body is exported; it may contain account data.
            $state.State='LocalOrNetworkError';Publish-State
            try {Send-Json $context @{state='LocalOrNetworkError';status=-20} 500} catch {}
        } finally {$submission=$null;$body=$null;$reply=$null;$captcha=$null}
    }
} catch {
    $state.State='HelperStartFailed';Publish-State
} finally {
    if($listener){$listener.Close()};if($client){$client.Dispose()};$aes.Dispose()
    [Security.Cryptography.CryptographicOperations]::ZeroMemory($plaintext)
    [Security.Cryptography.CryptographicOperations]::ZeroMemory($key)
    $record=$null
    $state.State=if($state.GameAuthorizationReady){'Complete'}elseif($state.State -eq 'HelperStartFailed'){'HelperStartFailed'}else{'Stopped'}
    Publish-State
}
