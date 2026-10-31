#!/usr/bin/env python3
"""Отправка сообщения в Telegram и/или Discord (если заданы секреты репозитория), иначе печатает в лог.
  python tools/ci/notify.py "Текст сообщения"
Секреты GitHub: TELEGRAM_BOT_TOKEN + TELEGRAM_CHAT_ID  и/или  DISCORD_WEBHOOK_URL.
"""
import json
import os
import sys
import urllib.parse
import urllib.request


def send(text):
    sent = False
    tok, chat = os.environ.get("TELEGRAM_BOT_TOKEN"), os.environ.get("TELEGRAM_CHAT_ID")
    if tok and chat:
        data = urllib.parse.urlencode({"chat_id": chat, "text": text[:4000], "disable_web_page_preview": "true"}).encode()
        try:
            urllib.request.urlopen(f"https://api.telegram.org/bot{tok}/sendMessage", data, timeout=20)
            sent = True
        except Exception as e:  # noqa: BLE001
            print("Telegram: ошибка", e)
    hook = os.environ.get("DISCORD_WEBHOOK_URL")
    if hook:
        req = urllib.request.Request(hook, json.dumps({"content": text[:1900]}).encode(), {"Content-Type": "application/json"})
        try:
            urllib.request.urlopen(req, timeout=20)
            sent = True
        except Exception as e:  # noqa: BLE001
            print("Discord: ошибка", e)
    if not sent:
        print("(уведомления не настроены — сообщение только в логе)\n" + text)
    return sent


if __name__ == "__main__":
    send(" ".join(sys.argv[1:]) or "(пусто)")
