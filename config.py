# config.py
def can_build(env, platform):
    return platform == "windows" # https://github.com/pond3r/ggpo#building

def configure(env):
    env.Append(CPPPATH=["#modules/ggpo/include"])
    env.Append(LIBPATH=["#modules/ggpo/bin"])
    env.Append(LINKFLAGS=["GGPO.lib"])

def get_doc_path():
    return "doc_classes"

def get_doc_classes():
    return [
        "GGPO"
    ]