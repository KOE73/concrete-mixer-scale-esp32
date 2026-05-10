# SCons/PlatformIO post-script для автоматической прошивки web-раздела
# сразу после прошивки основной firmware.
#
# Сейчас этот скрипт отключен в platformio.ini: строка
# `post:scripts/upload_www_after_firmware.py` закомментирована. Чтобы вернуть
# прежнее поведение, раскомментируй ее.

import subprocess
import sys

# PlatformIO extra_scripts исполняются внутри SCons. Import("env") достает
# объект окружения сборки PlatformIO/SCons, через который можно читать переменные
# проекта и регистрировать действия после целей сборки.
Import("env")

# Integration dump - внутренний режим PlatformIO, где скрипт не должен менять
# окружение сборки. В обычной локальной сборке это условие не срабатывает.
if env.IsIntegrationDump():
    Return()


def upload_www_after_firmware(source, target, env):
    # Если пользователь явно запустил `uploadfs`, не запускаем `uploadfs`
    # повторно из post-action, иначе получится рекурсия/двойная прошивка.
    if "uploadfs" in COMMAND_LINE_TARGETS:
        return

    # Берем корень проекта и имя окружения из текущего запуска PlatformIO,
    # чтобы не дублировать путь проекта и env:matrix-portal-s3 в коде.
    project_dir = env.subst("$PROJECT_DIR")
    pio_env = env.subst("$PIOENV")

    # Собираем команду:
    # python -m platformio run --project-dir <project> -e <env> -t uploadfs
    # Цель uploadfs берет каталог из `data_dir = www` в platformio.ini.
    command = [
        sys.executable,
        "-m",
        "platformio",
        "run",
        "--project-dir",
        project_dir,
        "-e",
        pio_env,
        "-t",
        "uploadfs",
    ]

    # Если PlatformIO уже знает порт прошивки, передаем его вложенному запуску,
    # чтобы firmware и web-раздел шились в одно и то же устройство.
    upload_port = env.subst("$UPLOAD_PORT")
    if upload_port and "$" not in upload_port:
        command.extend(["--upload-port", upload_port])

    # Запускаем отдельный PlatformIO-процесс. check_call пробросит ошибку наружу,
    # если сборка/прошивка web-раздела завершится неуспешно.
    print("Uploading www SPIFFS partition after firmware upload...")
    subprocess.check_call(command, cwd=project_dir)


# Регистрируем функцию как действие после цели `upload`, то есть после прошивки
# основной firmware. Пока скрипт закомментирован в platformio.ini, эта строка
# не выполняется.
env.AddPostAction("upload", upload_www_after_firmware)
