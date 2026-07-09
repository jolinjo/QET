@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

REM ================================================================
REM  重啟 Gitea 服務並驗證 git 子程序是否恢復（解 0xc0000142）
REM  用法：放在 hc-server 桌面，雙擊執行（會自動要求系統管理員權限）
REM ================================================================

REM --- 自我提權：沒有系統管理員權限就用 UAC 重新啟動自己 ---
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo 需要系統管理員權限，正在提權…
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

set "BASE=http://localhost:3000"
set "REPO=HC-Git/HC_Electrical-Schematics"

echo ============================================================
echo   重啟 Gitea 服務並驗證 git 子程序（0xc0000142 修復）
echo ============================================================
echo.

echo [1/3] 重啟 gitea 服務…
powershell -NoProfile -Command "try { Restart-Service gitea -Force -ErrorAction Stop; Write-Host '      服務已重啟' -ForegroundColor Green } catch { Write-Host '      Restart-Service 失敗，改用 sc 強制停/啟…' -ForegroundColor Yellow; sc.exe stop gitea | Out-Null; Start-Sleep 3; sc.exe start gitea | Out-Null }"
echo.

echo [2/3] 等待 Gitea 網頁回應…
powershell -NoProfile -Command "for($i=1;$i -le 30;$i++){ try{ $r=Invoke-WebRequest -UseBasicParsing -TimeoutSec 5 '%BASE%/api/healthz'; if($r.StatusCode -eq 200){ Write-Host '      Gitea 已回應（healthz 200）'; exit } }catch{}; Start-Sleep 3 }; Write-Host '      逾時：Gitea 仍未回應，請檢查服務狀態' -ForegroundColor Red"
echo.

echo [3/3] 驗證會 spawn git 的端點（healthz 200 不代表 git 正常，一定要驗這個）
echo       需要一組 Gitea token（任何有此 repo 讀取權限的帳號皆可）
set /p "TOKEN=      貼上 token 後按 Enter（直接 Enter 可略過驗證）: "
if "%TOKEN%"=="" (
    echo.
    echo       已略過驗證。請直接在 QET「圖檔管理」試一次「入庫送審」確認。
    goto end
)
echo.
powershell -NoProfile -Command "$h=@{Authorization='token %TOKEN%'}; try{ $r=Invoke-WebRequest -UseBasicParsing -Headers $h -TimeoutSec 10 '%BASE%/api/v1/repos/%REPO%/branches'; if($r.StatusCode -eq 200){ Write-Host '      成功：git 子程序正常（branches 200），可正常使用！' -ForegroundColor Green } } catch { $code=$_.Exception.Response.StatusCode.value__; if($code -eq 500){ Write-Host '      仍是 500：git 子程序還是起不來（0xc0000142）。' -ForegroundColor Red; Write-Host '      單純重啟服務沒清乾淨桌面堆積，建議整台重開機（Restart-Computer -Force）。' -ForegroundColor Red } else { Write-Host ('      驗證失敗：'+$_.Exception.Message) -ForegroundColor Red } }"

:end
echo.
echo ------------------------------------------------------------
pause
