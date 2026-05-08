import os

Import("env")

if env.IsIntegrationDump():
    Return()

project_dir = env.subst("$PROJECT_DIR")
wrapper = os.path.join(project_dir, "scripts", "esptool_no_py_warning.py")

env.Replace(
    OBJCOPY=wrapper,
    UPLOADER=wrapper,
)
