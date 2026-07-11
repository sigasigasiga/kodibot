# kodibot

Telegram bot that lets you stream video files directly to Kodi.

https://github.com/user-attachments/assets/fef4d32d-eff1-46c2-94fb-4693ed29d34d

# Setting it up

Note that in these instructions it is assumed that you are running kodibot on the same machine as your Kodi instance. If you are running kodibot on a different machine, you will need to adjust the configuration accordingly.

## Obtaining credentials

### 1. Enable remote control in Kodi

Enabling remove control via HTTP is required for kodibot to communicate with Kodi. Follow these steps:

1. **Open Kodi Settings:**
   - Navigate to `Settings` → `Services` → `Control`

2. **Enable Web Server:**
   - Toggle `Allow remote control via HTTP` to **ON**
   - Keep the default port `8080`
   - Change the values of the `Username` and `Password` fields if you want and remember them—you'll need them for the bot configuration

### 2. Create a Telegram App

To use the Telegram Bot API, you need to create an app on **my.telegram.org**:

1. **Visit:** https://my.telegram.org

2. **Login:**
   - Sign in with your phone number and verify via SMS or Telegram app

3. **Navigate to API Development Tools:**
   - Click on `API development tools`

4. **Create a New Application:**
   - Fill in the form with:
     - **App title:** any name you like, it won't be visible to you
     - **Short name:** any name you like, it won't be visible to you
     - **Platform:** Select `Desktop` (or appropriate platform)
   - Accept the ToS and click `Create My App!`

5. **Copy Your Credentials:**
   - You'll receive two important values:
     - **API ID** (`api_id`)
     - **API Hash** (`api_hash`)
   - Store these securely—you'll need them in the configuration

### 3. Create a Telegram Bot with BotFather

1. **Start BotFather:**
   - Open Telegram and search for `@BotFather`
   - Send `/start`

2. **Create a New Bot:**
   - Send `/newbot`
   - Choose a name for your bot
   - Choose a username for your bot (must end with `bot`)

3. **Receive Your Token:**
   - BotFather will send you a message with your **Bot Token**
   - Format: `123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefgh`
   - Store this securely—you'll need it in the configuration

### 4. Obtain Your Telegram User ID

1. Open Telegram and search for `@userinfobot` or visit https://t.me/userinfobot
2. Send `/start`
3. The bot will reply with your **User ID** (a numeric value). Keep this ID for the whitelist configuration.

## Installation

Currently we have installation instructions only for Linux using Podman.

Note that it is possible to run kodibot on other platforms (including Windows and macOS), but you will need to adapt the instructions yourself.
If you have successfully run kodibot on another platform, please consider contributing your instructions to this README, I'll be happy to include them.

### Installation on Linux with Podman

#### Prerequisites

- Podman installed (https://podman.io/docs/installation)
- systemd-based Linux distribution (any mainstream distro should work)

#### Steps

1. **Download kodibot podman quadlet:**
```bash
wget https://github.com/sigasigasiga/kodibot/raw/refs/heads/main/podman/kodibot.container
```

If you intend to run kodibot on a machine different from your Kodi instance, you may need to edit the `kodibot.container` file and change the IP address to the one that is reachable from your Kodi machine in the `PublishPort` field.

2. **Create the configuration file:**

- Copy the template:

```bash
wget https://github.com/sigasigasiga/kodibot/raw/refs/heads/main/podman/kodibot.conf.template -O kodibot.conf
```

- Edit `kodibot.conf` and fill in your credentials. Required fields:
  - `telegram-api-id=` — Your API ID from my.telegram.org
  - `telegram-api-hash=` — Your API Hash from my.telegram.org
  - `telegram-bot-token=` — Your bot token from BotFather
  - `telegram-user-whitelist=` — Comma-separated list of Telegram user IDs allowed to use the bot
  - `kodi-username=` — Kodi HTTP RPC username
  - `kodi-password=` — Kodi HTTP RPC password

3. **Create the Podman secret:**

```bash
podman secret create kodibot-credentials kodibot.conf
```

4. **Install the Quadlet:**

```bash
podman quadlet install kodibot.container
```

5. **Start the service:**

```bash
systemctl --user start kodibot
```

6. **Verify it's running:**

```bash
systemctl --user status kodibot
```

## Security Notes

- Use the `telegram-user-whitelist` to restrict bot access to trusted users only
- Ensure your Kodi instance is not exposed to untrusted networks
