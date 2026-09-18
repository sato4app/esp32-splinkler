## esp32-splinkler - ESP32からBluetooth経由でSplinklerを制御

ESP32-C3からBluetooth経由でSplinklerを制御するWebアプリ
実行日時や頻度、動作の継続時間を指定可能

回路側が未作成のため、現在は以下の2つを実装している。

- 動作スケジュールの作成（Webのみ。ESP32へは送信せず、画面上で進行を表示する）
- 接続テストとして、Webで指定した回数だけ内蔵LEDを1秒間隔で点滅させる

## 動作スケジュール

| 項目 | 内容 |
|---|---|
| 間隔 | 日・時・分・秒で指定（日 0〜365、時 0〜23、分 0〜59、秒 0〜59）。1秒以上 |
| 動作時間 | 分・秒で指定（分 0〜999、秒 0〜59）。1秒以上で、回数が2回以上なら間隔より短くする |
| 回数 | 動作させる回数（1〜100） |
| 開始日時 | 1回目の動作を始める日時。初期値は現在日時＋60秒で、手で変更もできる |
| リセット | 開始日時を初期値（現在日時＋60秒）に戻す |
| スタート | 開始日時から間隔ごとに、回数分の動作日時を算出して一覧に出す |
| クリア | 動作予定を消す |

n回目の動作は「開始日時 + (n−1) × 間隔」に始まり、動作時間だけ続く。
開始日時が空欄のままスタートした場合は、リセットと同じく現在日時＋60秒を使う。
開始日時が過去の場合はエラーにする。

一覧では各回を「予定・次回・動作中・完了」で表示し、次回までの残り時間（動作中は終了までの残り時間）を1秒ごとに更新する。
入力値（開始日時を除く）と動作予定はブラウザ（localStorage）に保存し、ページを開き直しても続きから表示する。
開始日時はページを開くたびに初期値に戻る。

## ファイル構成

Web側（ブラウザ）とファーム側（ESP32-C3）を1つのリポジトリで管理する。
ファーム側はPlatformIOプロジェクトとして `firmware/` に分離している。

```
index.html                      Web側（動作スケジュール・Web Bluetooth接続・LED点滅テスト）
manifest.json                   PWA設定（アプリ名・アイコン・表示モード）
service-worker.js               オフライン起動用のキャッシュ制御
icons/                          PWAアイコン（192x192 / 512x512）
esp32-splinkler.code-workspace  VS Code 用（リポジトリと firmware を同時に開く）
firmware/                       ESP32ファーム（PlatformIOプロジェクト）
  platformio.ini                ボード・ビルド設定
  src/main.cpp                  BLEサーバ（Nordic UART Service 準拠）・LED点滅制御
```

## 通信仕様

Nordic UART Service のUUIDを使い、テキストのコマンドをやり取りする。
デバイス名は `ESP32-Splinkler`。

| 用途 | UUID |
|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX（Web → ESP32） | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX（ESP32 → Web） | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

Web → ESP32

| コマンド | 内容 |
|---|---|
| `BLINK <回数>` | 1秒点灯・1秒消灯を指定回数（1〜100）繰り返す。点滅中に受けた場合は最初からやり直す |
| `STOP` | 点滅を止めて消灯する |

ESP32 → Web（BLEの既定MTUに収まるよう20バイト以内の英数字）

| 通知 | 内容 |
|---|---|
| `ON <n>/<回数>` | n回目の点灯を開始した |
| `DONE <回数>` | 指定回数の点滅が完了した |
| `STOPPED` | `STOP` で停止した |
| `ERR RANGE 1-100` | 回数が範囲外 |
| `ERR UNKNOWN` | 不明なコマンド |

## 開発・実行

### ファーム側（ESP32-C3）

VS Codeで `esp32-splinkler.code-workspace` を開き、PlatformIOでビルド・書き込みする。
CLIの場合:

```bash
pio run -d firmware                  # ビルド
pio run -d firmware -t upload        # 書き込み
pio device monitor -b 115200         # シリアルモニタ
```

シリアルモニタで `BLINK 3` や `STOP` と入力して改行しても、Webと同じように動作確認できる。

内蔵LEDは GPIO8 で、LOWで点灯する基板（ESP32-C3 Super Miniなど）を前提にしている。
待機中にLEDが点きっぱなしになる場合は、`main.cpp` の `LED_ON_LEVEL` と `LED_OFF_LEVEL` を入れ替える。

### Web側

Web Bluetooth APIを使うため、HTTPSまたはlocalhostで開く必要がある。

```bash
python -m http.server 8000
# または npx serve .
# ブラウザで http://localhost:8000 を開く
```

Chrome（Android/デスクトップ）で動作する。SafariとFirefoxはWeb Bluetooth非対応。

### Androidで使う（PWA）

1. GitHub Pages を有効にする（Settings → Pages → Branch: `main` / `(root)`）
2. Androidの Chrome で `https://sato4app.github.io/esp32-splinkler/` を開く
3. 画面の「ホーム画面に追加」ボタン、またはChromeメニューの「アプリをインストール」を選ぶ

- 端末のBluetoothをONにする（Android 11以下は位置情報もONが必要）
- 配信ファイルを変更したら `service-worker.js` の `CACHE_VERSION` を上げる
