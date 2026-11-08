# openwrt_mqtt_wol — MQTT → Wake-on-LAN для OpenWrt

Замена ESP8266-мостика: демон на роутере слушает MQTT-брокер и по команде
отправляет magic packets, плюс LuCI-интерфейс для настройки и ручной
«разбудки» устройств.

```
MQTT (топик устройства, payload "1"/"on"/"wake")
        │
        ▼
 mqtt-wol (демон, C + libmosquitto) ──► UDP broadcast, magic packet (102 байта)
        ▲
        │ ubus call mqtt-wol wake '{"target":"Имя"}'
 luci-app-mqtt-wol (LuCI: Настройки + Статус с кнопками Wake)
```

## Пакеты

| Пакет | Содержимое |
|---|---|
| `mqtt-wol` | демон `/usr/sbin/mqtt-wol`, `/etc/init.d/mqtt-wol`, конфиг UCI `/etc/config/mqtt-wol` |
| `luci-app-mqtt-wol` | LuCI: *Services → MQTT Wake-On-LAN* (вкладки Settings/Status), rpcd-ACL, ru-локализация |

Зависимости демона: `libmosquitto-ssl` (с TLS «из коробки»), `libuci`,
`libubus`, `libblobmsg-json`, `libpthread` — всё есть в официальных
репозиториях OpenWrt 21.02+. LuCI-приложение — классическое (Lua/CBI через
`luci-compat` + `luci-lua-runtime`; на 25.12 это обязательная зависимость,
она указана в `DEPENDS`), работает на 21.02—25.12 включительно.

## Возможности

- **Подписка на топики**: у каждого устройства свой топик; приходит
  `1` / `on` / `wake` / `true` → отправляется magic packet. Поддерживаются
  MQTT-маски `+` / `#`.
- **Отправка WOL**: UDP-порт (по умолчанию 9) и адрес(а) назначения
  (по умолчанию `255.255.255.255`, можно список через запятую:
  `192.168.1.255,255.255.255.255`) — глобально и переопределение на
  устройство. Это решает главную проблему ESP-версии: роутер всегда в нужном
  широковещательном домене.
- **ubus-интерфейс** (для скриптов, Home Assistant и т.п.):
  ```sh
  ubus call mqtt-wol status
  ubus call mqtt-wol wake '{"target":"Desktop PC"}'   # по имени или MAC
  ubus call mqtt-wol reload                            # перечитать UCI
  ```
- **Статус в MQTT** (необязательно): retained-публикации
  `{"client":"...","state":"online"}` / LWT-`offline` в `status_topic`.
- **LuCI**: настройка брокера (host/port/user/pass/clientid/keepalive/TLS),
  карточки устройств, живая страница статуса с кнопками **Wake**.
- **SIGHUP / `procd reload`** — конфиг перечитывается без перезапуска
  (триггер по изменению `/etc/config/mqtt-wol`).
- Автопереподключение MQTT (экспоненциальный backoff 2–30 c) и respawn через
  procd.

## Сборка

### Вариант А — обычное дерево OpenWrt

```sh
cp -r mqtt-wol           <openwrt>/package/net/
cp -r luci-app-mqtt-wol  <openwrt>/package/net/   # или в feeds/luci/applications/
make package/mqtt-wol/compile package/luci-app-mqtt-wol/compile V=s
```

### Вариант Б — SDK (на примере 25.12.6 mediatek/filogic, aarch64_cortex-a53)

```sh
# SDK берётся ровно под вашу прошивку (DISTRIB_RELEASE + DISTRIB_TARGET):
wget https://downloads.openwrt.org/releases/25.12.6/targets/mediatek/filogic/openwrt-sdk-25.12.6-mediatek-filogic_gcc-14.3.0_musl.Linux-x86_64.tar.zst
wget https://downloads.openwrt.org/releases/25.12.6/targets/mediatek/filogic/sha256sums
sha256sum -c --ignore-missing sha256sums

# 25.12 распространяется в .tar.zst — нужен zstd:
sudo apt install -y zstd
tar --zstd -xf openwrt-sdk-25.12.6-mediatek-filogic_gcc-14.3.0_musl.Linux-x86_64.tar.zst
cd openwrt-sdk-25.12.6-mediatek-filogic_gcc-14.3.0_musl

./scripts/feeds update -a && ./scripts/feeds install -a   # feed luci входит в SDK по умолчанию

cp -r ~/dev/openwrt_mqtt_wol/mqtt-wol package/
cp -r ~/dev/openwrt_mqtt_wol/luci-app-mqtt-wol package/

make package/mqtt-wol/{clean,compile} V=s
make package/luci-app-mqtt-wol/{clean,compile} V=s
# если в дереве есть feed luci — Makefile сам использует luci.mk и соберёт
# ещё и пакет переводов luci-i18n-mqtt-wol-ru

find bin -name "*.apk" -o -name "mqtt-wol*.ipk" -o -name "luci-app-mqtt-wol*.ipk"
```

> OpenWrt 25.12 собирается с **apk** вместо opkg (`USE_APK` по умолчанию y):
> на выходе SDK будут `.apk`-файлы. На 24.10 и старее — `.ipk`.

## CI: сборка на GitHub Actions

В репозитории лежит `.github/workflows/build-packages.yml`:

- на **push в `main`** собирает пакеты под роутер по умолчанию
  (OpenWrt `25.12.6`, target `mediatek/filogic`);
- через **Actions → Build OpenWrt packages → Run workflow** можно указать
  любую пару `version` + `profile` (например `24.10.2` + `ramips/mt7621`).

Workflow скачивает официальный SDK (с проверкой sha256 и кэшированием
архива), ставит feeds (включая `luci`, чтобы luci.mk собрал и пакет
переводов `luci-i18n-mqtt-wol-ru`), компилирует оба пакета и складывает
результат в артефакт `mqtt-wol-<profile>-<version>` (для 25.12 — файлы
`.apk`, для 24.10 и старее — `.ipk`).

После сборки:

```sh
# скачать артефакт из вкладки Run workflow → Artifacts, затем:
scp mqtt-wol-*.apk luci-app-mqtt-wol-*.apk root@192.168.1.1:/tmp/
ssh root@192.168.1.1
apk update
apk add --allow-untrusted /tmp/mqtt-wol*.apk /tmp/luci-app-mqtt-wol*.apk
/etc/init.d/mqtt-wol enable
```

## Установка на роутер

**OpenWrt 25.12 (apk):**
```sh
scp bin/packages/*/mqtt-wol*.apk luci-app-mqtt-wol*.apk root@192.168.1.1:/tmp/
ssh root@192.168.1.1
apk update
apk add --allow-untrusted /tmp/mqtt-wol*.apk /tmp/luci-app-mqtt-wol*.apk
/etc/init.d/mqtt-wol enable
```

**OpenWrt 24.10 и старее (opkg):**
```sh
scp bin/packages/*/packages/mqtt-wol_*.ipk luci-app-mqtt-wol_*.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1
opkg update
opkg install /tmp/mqtt-wol_*.ipk /tmp/luci-app-mqtt-wol_*.ipk
/etc/init.d/mqtt-wol enable
```

## Настройка

1. LuCI → **Services → MQTT Wake-On-LAN → Settings**: хост/порт/логин/пароль
   брокера (для `m8.wqtt.ru` порт 21236), добавить устройства (имя, MAC,
   топик) → Save & Apply (демон перезапустится сам).
2. Вкладка **Status**: видно подключение к брокеру и список устройств с
   кнопками **Wake**.

> ⚠️ clusterfly/wqtt: топики на хостинговом брокере обычно требуют
> полного имени с префиксом пользователя, например
> `u_XXXXXX/control/pcwol` — проверяйте, какой топик реально виден
> на брокере, и пишите его в настройках устройства целиком.

### Конфигурация через UCI/CLI

```sh
uci set mqtt-wol.main.host='m8.wqtt.ru'
uci set mqtt-wol.main.port='21236'
uci set mqtt-wol.main.username='u_XXXXXX'
uci set mqtt-wol.main.password='...'
uci add mqtt-wol device
uci -q batch <<'EOF'
set mqtt-wol.@device[-1].name='Desktop PC'
set mqtt-wol.@device[-1].mac='AC:DE:48:00:11:22'
set mqtt-wol.@device[-1].topic='control/pcwol'
commit mqtt-wol
EOF
/etc/init.d/mqtt-wol reload
```

Проверка парсинга конфига без запуска демона:

```sh
/usr/sbin/mqtt-wol -c          # показать, что понял UCI
/etc/init.d/mqtt-wol start; logread -e mqtt-wol
```

Проверка «вживую» с другой машины (нужен mosquitto-clients):

```sh
mosquitto_pub -h m8.wqtt.ru -p 21236 -u u_XXXXXX -P 'пароль' \
              -t 'control/pcwol' -m '1'
```

## Тесты в дереве (на dev-машине)

```sh
make test     # генерация magic packet: слушатель на 127.0.0.1:19009
make check    # синтакс-проверка C против заголовков uci/libubox/libubus/mosquitto
              # (заголовки в /tmp/opencode/inc)
```

## Структура

```
mqtt-wol/                 # демон (OpenWrt package)
  src/main.c              # mqtt-клиент, ubus, главный цикл
  src/config.[ch]         # загрузка /etc/config/mqtt-wol
  src/wol.[ch]            # magic packet
  files/mqtt-wol.init     # procd-скрипт
  files/mqtt-wol.config   # конфиг по умолчанию
luci-app-mqtt-wol/        # LuCI (OpenWrt package)
  root/usr/lib/lua/luci/controller/mqtt_wol.lua
  root/usr/lib/lua/luci/model/cbi/mqtt_wol/settings.lua
  root/usr/lib/lua/luci/view/mqtt_wol/status.htm
  root/usr/share/rpcd/acl.d/luci-app-mqtt-wol.json
  po/ru/mqtt-wol.po       # перевод
tools/                    # woltest — ручной запуск magic packet, python-слушатель
Makefile                  # dev-цели test/check
```

## Частые проблемы

**Нет пункта «MQTT Wake-On-LAN» в Services (25.12):**
в новых релизах базовая LuCI работает на ucode, и для классических
Lua/CBI-приложений нужен пакет `luci-compat` (он тянет `luci-lua-runtime`) —
начиная с этой версии он указан в зависимостях `luci-app-mqtt-wol`. Для уже
установленной связки достаточно:
```sh
apk add luci-compat
rm -rf /tmp/luci-modulecache /tmp/luci-indexcache*
/etc/init.d/uhttpd restart
```
Проверка: `logread | grep -i "no Lua runtime"` — сообщение uhttpd означает
ровно отсутствие этого пакета.

**`broker refused connection (code 5: not authorised)`** в `logread`:
демон подключается анонимно — задайте логин/пароль брокера:
```sh
uci set mqtt-wol.main.username='u_XXXXXX'
uci set mqtt-wol.main.password='ВАШ_ПАРОЛЬ'
uci set mqtt-wol.@device[0].mac='AA:BB:CC:DD:EE:FF'
uci commit mqtt-wol
/etc/init.d/mqtt-wol restart
logread -e mqtt-wol   # ждём "mqtt: connected to m8.wqtt.ru:21236"
```
(то же самое доступно в LuCI: Settings). Если с корректными учётными
данными код 5 сохраняется — проверьте на брокере, не привязан ли слот
только к определённому clientid, и используйте MQTT v3.1.1-совместимый порт.

## Известные ограничения / идеи

- Пароль MQTT хранится в открытом виде в `/etc/config/mqtt-wol`
  (стандартное поведение OpenWrt-пакетов; ограничьте доступ к LuCI/SSH).
- Если ПК и роутер в разных VLAN — добавьте directed broadcast нужной
  подсети в «Broadcast address» (и убедитесь, что свич его не режет).
- Идея на v2: публикация онлайн-статуса устройств по ARP ping после wake.
