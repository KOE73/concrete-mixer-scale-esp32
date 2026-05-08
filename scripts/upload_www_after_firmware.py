import subprocess
import sys

Import("env")

if env.IsIntegrationDump():
    Return()


def upload_www_after_firmware(source, target, env):
    if "uploadfs" in COMMAND_LINE_TARGETS:
        return

    project_dir = env.subst("$PROJECT_DIR")
    pio_env = env.subst("$PIOENV")
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
    upload_port = env.subst("$UPLOAD_PORT")
    if upload_port and "$" not in upload_port:
        command.extend(["--upload-port", upload_port])

    print("Uploading www SPIFFS partition after firmware upload...")
    subprocess.check_call(command, cwd=project_dir)


env.AddPostAction("upload", upload_www_after_firmware)
