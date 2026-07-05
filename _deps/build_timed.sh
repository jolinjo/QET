#!/usr/bin/env bash
# 自我監控建置：記時間戳、到預期時間自動檢查、辨識是否卡住/出錯。
# 用法: build_timed.sh <預期秒數> [說明]
# 設計成用 run_in_background 跑；輪詢與 sleep 都在背景腳本內（前景 sleep 被禁）。
set -u
EXP=${1:-60}
DESC=${2:-build}
QET=/c/Users/JasonLin/Documents/ClaudeCode/QET
QWIN='C:\Users\JasonLin\Documents\ClaudeCode\QET'
LOG=$QET/_deps/bt.log
cd "$QET"

# 1) 確保單一 build：先清殘留（見記憶 qet-build-one-at-a-time）
taskkill //F //IM ninja.exe //IM cl.exe //IM cmake.exe >/dev/null 2>&1
sleep 1

START=$(date +%s)
echo "[$(date +%H:%M:%S)] 開始「$DESC」，預期 ~${EXP}s，硬上限 $((EXP*3))s"

# 2) 背景跑 ninja
cmd //c "call $QWIN\\winenv.bat && cd /d $QWIN && ninja -C build" > "$LOG" 2>&1 &
NPID=$!

# 卡死判定改用「進度停滯」而非絕對時間：有在前進(ninja [N/M] 有變)就不砍，
# 只有連續 STALL_LIMIT 秒沒任何進度、且沒有 cl.exe 在跑，才判定卡死。
DEADLINE=$((START+EXP)); WARNED=0
STALL_LIMIT=180                 # 連續無進度超過這麼久才算卡死
BACKSTOP=$((START+EXP*6+600))   # 極端保險上限，避免無限等待
LAST_PROG=""; LAST_CHANGE=$START
while kill -0 "$NPID" 2>/dev/null; do
  sleep 5
  NOW=$(date +%s); EL=$((NOW-START))
  CUR=$(tail -1 "$LOG" | tr -d '\0')
  if [ "$CUR" != "$LAST_PROG" ]; then LAST_PROG="$CUR"; LAST_CHANGE=$NOW; fi
  STALL=$((NOW-LAST_CHANGE))
  NCL=$(tasklist //FI "IMAGENAME eq cl.exe" 2>/dev/null | grep -ci "cl.exe")
  # 到預期時間仍沒完成 -> 診斷一次（僅提示，不中止；慢≠卡死）
  if [ "$NOW" -ge "$DEADLINE" ] && [ "$WARNED" -eq 0 ]; then
    WARNED=1
    echo "[${EL}s] ⚠ 已達預期(${EXP}s)仍未完成（仍在前進，不中止）："
    echo "   cl.exe 數量: $NCL"
    if grep -qiE "kwidgetsaddons|kcoreaddons|KF6[A-Za-z]" "$LOG"; then
      echo "   ‼ 偵測到 KF6 在重編（純程式/版號改動不該發生，可能被 reconfigure 觸發全量重編）"
    fi
    echo "   最新進度: $CUR"
  fi
  # 真正卡死：長時間無進度且無編譯行程
  if [ "$STALL" -ge "$STALL_LIMIT" ] && [ "$NCL" -eq 0 ]; then
    echo "[${EL}s] ⛔ 連續 ${STALL}s 無進度且無 cl.exe，判定卡死，中止。停在: $CUR"
    taskkill //F //IM ninja.exe //IM cl.exe >/dev/null 2>&1
    break
  fi
  if [ "$NOW" -ge "$BACKSTOP" ]; then
    echo "[${EL}s] ⛔ 觸及極端保險上限，中止。停在: $CUR"
    taskkill //F //IM ninja.exe //IM cl.exe >/dev/null 2>&1
    break
  fi
done
wait "$NPID"; RC=$?
END=$(date +%s); EL=$((END-START))

echo "======================================"
echo "「$DESC」 exit=$RC  實際 ${EL}s / 預期 ${EXP}s"
if grep -qiE "error C[0-9]|error LNK|FAILED:" "$LOG"; then
  echo "結果: ❌ 編譯錯誤"
  grep -iE "error C[0-9]|error LNK|FAILED:" "$LOG" | tr -d '\0' | head -5
elif [ "$RC" -ne 0 ]; then
  echo "結果: ❌ 非零結束（被中止/卡死/環境問題），看 log 尾:"
  tail -3 "$LOG" | tr -d '\0'
elif [ "$EL" -gt $((EXP*2)) ]; then
  echo "結果: ⚠ 成功但比預期慢 2 倍以上，值得留意是否有並發或全量重編"
else
  echo "結果: ✅ 正常完成（在預期範圍內）"
fi
cp build/bin/*.dll build/ >/dev/null 2>&1
echo "版本: $(./build/qelectrotech.exe --version 2>&1 | tr -d '\0' | head -1)"
