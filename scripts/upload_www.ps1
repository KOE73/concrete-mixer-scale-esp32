# Ручная команда для сборки и прошивки web-раздела SPIFFS.
# Важно: сам каталог web-файлов здесь не указан. PlatformIO берет его из
# `data_dir = www` в platformio.ini, а цель `uploadfs` собирает и прошивает
# именно этот data-dir в раздел, заданный через board_build.filesystem/partitions.csv.

# Останавливаем скрипт при первой ошибке, чтобы не продолжать после
# неудачной сборки или неудачной прошивки.
$ErrorActionPreference = "Stop"

# $PSScriptRoot - папка, где лежит этот .ps1, то есть scripts.
# Родительская папка scripts - корень проекта с platformio.ini.
$ProjectDir = Split-Path -Parent $PSScriptRoot

# Имя окружения из platformio.ini: [env:matrix-portal-s3].
$Environment = "matrix-portal-s3"

# Команда PlatformIO:
# - run: запустить задачу проекта;
# - --project-dir: явно указать корень проекта, чтобы скрипт работал из любой папки;
# - -e: выбрать окружение платы;
# - -t uploadfs: собрать файловую систему из data_dir и прошить web-раздел.
$Command = @(
    "run",
    "--project-dir", $ProjectDir,
    "-e", $Environment,
    "-t", "uploadfs"
)

# Все аргументы, переданные этому скрипту, пробрасываем в PlatformIO.
# Например: .\scripts\upload_www.ps1 --upload-port COM5
if ($args.Count -gt 0) {
    $Command += $args
}

# Запускаем platformio с массивом аргументов. Конструкция @Command передает
# элементы массива как отдельные аргументы, а не одной длинной строкой.
platformio @Command
