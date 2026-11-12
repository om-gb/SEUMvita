"""Load a Seum_Data folder with MonoBehaviour type trees generated from its
Managed DLLs (the build ships without them)."""
import os
import UnityPy
from UnityPy.helpers.TypeTreeGenerator import TypeTreeGenerator

UNITY_VERSION = "2018.3.7f1"


class FixedTypeTreeGenerator(TypeTreeGenerator):
    """TypeTreeGeneratorAPI puts the 4-byte align flag on m_GameObject instead
    of m_Enabled, shifting every field after it by 3 bytes. Move it."""

    def get_nodes(self, assembly, fullname):
        nodes = super().get_nodes(assembly, fullname)
        for n in nodes:
            if n.m_Level == 1 and n.m_Name == "m_Enabled":
                n.m_MetaFlag |= 0x4000
        return nodes


def load(data_dir):
    """data_dir is the Seum_Data folder; its parent must be the game root."""
    gen = FixedTypeTreeGenerator(UNITY_VERSION)
    gen.load_local_dll_folder(os.path.join(data_dir, "Managed"))
    env = UnityPy.load(data_dir)
    env.typetree_generator = gen
    return env
