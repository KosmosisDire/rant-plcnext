"""Writes iec/ProjectItems/LibraryDescription.xml, the folders Engineer's sidebar shows the library in:
a folder per block family, and Rant Math's calls under Math by the folders its RantMath.json names.

    library_description.py <math tree> <out file>
"""
import glob
import json
import os
import re
import sys
from xml.sax.saxutils import quoteattr

IEC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "iec")

# Each block's folder, in the order the sidebar lists them.
BLOCKS = {
    "Node": "Node", "LogLine": "Node", "Status": "Node",
    "Publisher": "Topics", "Subscriber": "Topics",
    "FunctionDefinition": "Functions", "RemoteFunction": "Functions",
    "VariableDefinition": "Variables", "RemoteVariable": "Variables",
    "TaskDefinition": "Tasks", "RemoteTask": "Tasks",
    "Mirror": "Mirror",
}


def blocks():
    """The function blocks and functions the C# here declares."""
    found = []
    for path in sorted(glob.glob(os.path.join(IEC, "*.cs"))):
        text = open(path, encoding="utf-8").read()
        found += re.findall(r"\[(?:FunctionBlock|Function)\]\s*public (?:static )?class (\w+)", text)
    return found


def main():
    math, out = sys.argv[1:3]
    folders = {f: [] for f in BLOCKS.values()}
    for name in blocks():
        if name not in BLOCKS:
            sys.exit(f"library_description.py: give the block {name} a folder in BLOCKS")
        folders[BLOCKS[name]].append(name)
    with open(os.path.join(math, "bindings", "plcnext", "RantMath.json"), encoding="utf-8") as f:
        for folder, names in json.load(f).items():
            folders["Math\\" + folder.replace("/", "\\")] = names
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<Library Description="Rant">', '  <ToolboxCategories UseNameAsPath="True">']
    lines += [f"    <ToolboxCategory Id={quoteattr(f)} Name={quoteattr(f)}/>" for f in folders]
    lines += ["  </ToolboxCategories>", "  <ProgramOrganizationUnits>"]
    lines += [f"    <ProgramOrganizationUnit Name={quoteattr(n)} ToolboxCategoryId={quoteattr(f)}/>" for f, names in folders.items() for n in names]
    lines += ["  </ProgramOrganizationUnits>", "</Library>", ""]
    text = "\r\n".join(lines)
    # unchanged stays untouched, so MSBuild sees nothing new
    if not os.path.exists(out) or open(out, encoding="utf-8", newline="").read() != text:
        with open(out, "w", encoding="utf-8", newline="") as f:
            f.write(text)


if __name__ == "__main__":
    main()
