"""将仓库法律正文导出为官网静态页面，不部署或修改服务器。"""
from pathlib import Path
import argparse
import hashlib
import html
import json
import re

CODE_ROOT = Path(__file__).resolve().parents[2]
PAGES = {
    "用户协议.md": "privacy-policy/coral-pinyin/user-agreement/index.html",
    "隐私政策.md": "privacy-policy/coral-pinyin/privacy-policy/index.html",
    "许可说明.md": "license.html",
}


def inline(text):
    # 先保护内联元素，再转义普通正文；不接受 Markdown 中的原始 HTML。
    tokens = []

    def token(fragment):
        tokens.append(fragment)
        return f"\x00{len(tokens) - 1}\x00"

    text = re.sub(r"`([^`]+)`", lambda m: token("<code>" + html.escape(m[1]) + "</code>"), text)

    def link(match):
        label, target = match.groups()
        if target in PAGES:
            target = "/" + PAGES[target].removesuffix("index.html")
        if not target.startswith(("https://", "/")) or target.startswith("//"):
            raise ValueError("不支持的法律页面链接: " + target)
        return token('<a href="' + html.escape(target, quote=True) + '">' + html.escape(label) + "</a>")

    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, text)
    text = html.escape(text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    return re.sub(r"\x00(\d+)\x00", lambda m: tokens[int(m[1])], text)


def render_markdown(source):
    lines = source.splitlines()
    output = []
    index = 0
    while index < len(lines):
        line = lines[index].strip()
        if not line:
            index += 1
            continue
        if line.startswith("|"):
            rows = []
            while index < len(lines) and lines[index].strip().startswith("|"):
                cells = lines[index].strip().strip("|").split("|")
                if not all(re.fullmatch(r"\s*:?-+:?\s*", cell) for cell in cells):
                    rows.append(cells)
                index += 1
            output.append('<div class="legal-table"><table><thead><tr>' +
                          "".join("<th>" + inline(c.strip()) + "</th>" for c in rows[0]) + "</tr></thead><tbody>")
            for row in rows[1:]:
                output.append("<tr>" + "".join("<td>" + inline(c.strip()) + "</td>" for c in row) + "</tr>")
            output.append("</tbody></table></div>")
            continue
        heading = re.match(r"^(#{1,3}) (.+)$", line)
        if heading:
            level = len(heading[1])
            output.append(f"<h{level}>" + inline(heading[2]) + f"</h{level}>")
        elif line.startswith("- "):
            output.append("<ul>")
            while index < len(lines) and lines[index].strip().startswith("- "):
                output.append("<li>" + inline(lines[index].strip()[2:]) + "</li>")
                index += 1
            output.append("</ul>")
            continue
        else:
            paragraph = [line]
            index += 1
            while index < len(lines) and lines[index].strip() and not re.match(r"^(#|\||- )", lines[index].strip()):
                paragraph.append(lines[index].strip())
                index += 1
            output.append("<p>" + "<br>".join(inline(p) for p in paragraph) + "</p>")
            continue
        index += 1
    return "\n".join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    if output == CODE_ROOT or CODE_ROOT in output.parents:
        parser.error("官网输出目录应位于客户端仓库之外")
    checksums = {}
    for source_name, route in PAGES.items():
        source = (CODE_ROOT / "docs/legal" / source_name).read_text(encoding="utf-8-sig")
        title = source.splitlines()[0].removeprefix("# ")
        checksum = hashlib.sha256(source.encode("utf-8")).hexdigest()
        body = render_markdown(source)
        page = f'''<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="description" content="{html.escape(title, quote=True)}：个人开发者 Coral-IME，联系 coralime@126.com。">
<title>{html.escape(title)}</title><link rel="icon" href="/assets/img/logo-tile.svg" type="image/svg+xml">
<link rel="stylesheet" href="/assets/css/style.css"><link rel="stylesheet" href="/assets/css/legal.css">
</head><body><a class="legal-skip" href="#legal-content">跳到正文</a>
<header class="site-header"><div class="container header-inner"><a class="brand" href="/index.html"><img src="/assets/img/logo.svg" width="30" height="30" alt=""><span>珊瑚输入法</span></a><a href="/index.html">返回官网</a></div></header>
<main id="legal-content" class="legal-document"><nav aria-label="法律文件"><a href="/privacy-policy/coral-pinyin/user-agreement/">用户协议</a><a href="/privacy-policy/coral-pinyin/privacy-policy/">隐私政策</a><a href="/license.html">许可说明</a><button type="button" onclick="window.print()">打印 / 保存 PDF</button></nav>
<article>{body}</article></main><footer class="legal-footer">个人开发者（公开署名：Coral-IME） · <a href="mailto:coralime@126.com">coralime@126.com</a><p>首次正式发布时生效，具体日期随正式发行公告公布。</p></footer>
<!-- 法律正文源文件 SHA-256（规范换行为 LF）：{checksum} -->
</body></html>'''
        dest = output / route
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes((page + "\n").encode("utf-8"))
        checksums[source_name] = {"route": route, "source_sha256_lf": checksum}
    (output / "legal-content.json").write_text(json.dumps(checksums, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("已导出 3 份法律文件；未部署。")


if __name__ == "__main__":
    main()
