import argparse
from pathlib import Path
import re

from tree_sitter_language_pack import get_parser


LANGUAGES = {".sh": "bash", ".js": "javascript", ".css": "css", ".html": "html"}


def embedded_code(data):
    def visit(node):
        if node.type == "raw_text" and node.parent.type in {"script_element", "style_element"}:
            suffix = ".js" if node.parent.type == "script_element" else ".css"
            yield node.start_byte, node.end_byte, suffix
        for child in node.children:
            yield from visit(child)

    return list(visit(get_parser("html").parse(data).root_node))


def comment_nodes(node, data):
    if "comment" in node.type:
        if not data[node.start_byte:node.end_byte].startswith(b"#!"):
            yield node
    else:
        for child in node.children:
            yield from comment_nodes(child, data)


def tokens(node, data):
    if "comment" in node.type:
        return ()
    if not node.children:
        return ((node.type, data[node.start_byte:node.end_byte]),)
    return tuple(token for child in node.children for token in tokens(child, data))


def strip(data, suffix):
    if suffix == ".bat":
        return re.sub(rb"(?im)^[ \t]*(?:rem(?:[ \t].*)?|::[^\r\n]*)\r?$", b"", data)
    if suffix == ".prop":
        return re.sub(rb"(?m)^[ \t]*#[^\r\n]*\r?$", b"", data)
    parser = get_parser(LANGUAGES[suffix])
    root = parser.parse(data).root_node
    result = data
    for node in reversed(list(comment_nodes(root, data))):
        value = data[node.start_byte:node.end_byte]
        replacement = b"".join(re.findall(rb"\r\n|[\r\n]", value)) or b" "
        result = result[:node.start_byte] + replacement + result[node.end_byte:]
    if tokens(root, data) != tokens(parser.parse(result).root_node, result):
        raise ValueError("Removing comments changed program tokens")
    if suffix == ".html":
        for start, end, language in reversed(embedded_code(result)):
            result = result[:start] + strip(result[start:end], language) + result[end:]
    return result


def process(root, check=False):
    changed = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix not in {*LANGUAGES, ".bat", ".prop"}:
            continue
        data = path.read_bytes()
        result = strip(data, path.suffix)
        if result != data:
            changed.append(path.relative_to(root))
            if not check:
                path.write_bytes(result)
    if check and changed:
        raise ValueError("Comments remain in: " + ", ".join(map(str, changed)))
    return changed


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.directory.is_dir():
        parser.error("directory does not exist")
    for path in process(args.directory, args.check):
        print(path)
