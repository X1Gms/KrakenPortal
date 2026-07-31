# KrakenPortal — ESP32-S3 Skylander Remote Portal

Firmware completo em **C++ / PlatformIO** para transformar a tua placa **ESP32-S3-N16R8** num **Portal of Power (Traptanium Portal)** nativo via **USB HID**, controlado remotamente pela tua aplicação móvel Flutter via **HTTP REST API**.

---

## 🚀 Como Compilar e Gravar no ESP32-S3 (PlatformIO)

### 1. Instalar a extensão no VS Code
1. Abre o VS Code.
2. Vai a **Extensions** (`Ctrl+Shift+X`) e procura por **PlatformIO IDE**.
3. Clica em **Install** e reinicia o VS Code.

### 2. Abrir o Projeto e Configurar o WiFi
1. Abre a pasta `KrakenPortal` no VS Code.
2. Abre o ficheiro `include/PortalConfig.h`:
   - Altera `WIFI_SSID` e `WIFI_PASSWORD` para os dados da tua rede de casa.
   - Se o router da tua casa usar uma gama diferente de IP (ex: `192.168.0.x`), ajusta o `PORTAL_IP` (por omissão `192.168.1.200`).

### 3. Compilar e Fazer Upload
1. Liga o teu ESP32-S3 ao PC por cabo USB (porta USB CDC/UART).
2. No VS Code, clica no ícone da **PlatformIO** (cabeça de alien no menu esquerdo).
3. Na secção `Project Tasks -> esp32-s3-devkitc-1`, clica em:
   - **Build** (para compilar)
   - **Upload** (para gravar na placa)
   - **Monitor** (para ver as mensagens na consola de série a 115200 baud)

---

## 🎮 Como Ligar à Consola / Emulador (PS3 / Wii U / RPCS3 / CEMU)

1. Liga o cabo da porta **USB OTG Nativa** do ESP32-S3 diretamente à tua consola ou PC.
2. O sistema reconhecerá instantaneamente um **Traptanium Portal** (`VID 0x1430`, `PID 0x0150`).
3. Abre o teu jogo (*Trap Team, Swap Force, Giants, Spyro's Adventure, SuperChargers, Imaginators*).
4. Usa a tua app Flutter para enviar até **8 figuras em simultâneo** (Slots 1 a 8) e elas surgirão imediatamente dentro do jogo!

---

## 📡 Endpoints da API HTTP (`Porta 8080`)

| Método | Endpoint | Descrição |
|---|---|---|
| `GET` | `/slots` | Retorna um array JSON com o estado dos 16 slots |
| `POST` | `/slot/<1-16>` | Carrega um dump binário (1024 B) no slot especificado |
| `DELETE` | `/slot/<1-16>` | Remove o figura do slot e devolve o dump binário atualizado |
