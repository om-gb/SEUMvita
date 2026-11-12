"""Load a Seum_Data folder with MonoBehaviour type trees generated from its
Managed DLLs (the build ships without them)."""
import os
import UnityPy
from UnityPy.helpers.TypeTreeGenerator import TypeTreeGenerator

UNITY_VERSION = "2018.3.7f1"


ALIGN = 0x4000

class FixedTypeTreeGenerator(TypeTreeGenerator):
    """Fixes for TypeTreeGeneratorAPI output on this game:
    - the 4-byte align flag sits on m_GameObject instead of m_Enabled, which
      shifts every field after it by 3 bytes;
    - string[] / List<string> fields come out typed "string" instead of
      "vector", so the reader decodes the array as one string."""

    def get_nodes(self, assembly, fullname):
        nodes = list(super().get_nodes(assembly, fullname))
        for n in nodes:
            if n.m_Level == 1 and n.m_Name == "m_Enabled":
                n.m_MetaFlag |= ALIGN
        for i, n in enumerate(nodes[:-3]):
            # string -> Array -> size, data(string)  means an array of strings
            if (n.m_Type == "string" and nodes[i + 1].m_Type == "Array"
                    and nodes[i + 3].m_Level == n.m_Level + 2 and nodes[i + 3].m_Type == "string"):
                n.m_Type = "vector"
        return nodes


def load(data_dir):
    """data_dir is the Seum_Data folder; its parent must be the game root."""
    gen = FixedTypeTreeGenerator(UNITY_VERSION)
    gen.load_local_dll_folder(os.path.join(data_dir, "Managed"))
    env = UnityPy.load(data_dir)
    env.typetree_generator = gen
    return env
