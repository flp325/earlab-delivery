"""PRD-v0.2 可重复校验：结构、编号、一致性、Schema 合法性。

用法：python earLab/validate_prd.py
退出码 0 = 全部通过；1 = 存在失败项。
"""

import json
import re
import sys
from pathlib import Path

DOC = Path(__file__).with_name("PRD-v0.3.md")

results: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, detail: str = "") -> None:
    results.append((name, bool(cond), detail))


def main() -> int:
    if not DOC.exists():
        print(f"FAIL 文件不存在: {DOC}")
        return 1

    text = DOC.read_text(encoding="utf-8")
    lines = text.splitlines()

    # 1. 基本完整性
    check("文件存在且非空", len(text) > 8000, f"{len(text)} 字符 / {len(lines)} 行")
    check("标题版本为 v0.3", text.startswith("# EarLab 听感训练工作站") and "v0.3" in lines[0])

    # 2. 必需章节
    required = [
        "## 0.", "## 1.", "## 2.", "## 3.", "## 4. 范围", "## 5. 功能规格",
        "## 6. 技术规格", "## 7. 验收标准", "## 8. 里程碑", "## 9.",
        "## 10. 风险登记", "## 11. 待定问题", "## 12.", "## 附录 A",
    ]
    for heading in required:
        check(f"章节存在 {heading}", heading in text)

    # 3. 六个需求维度均已落纸
    dims = {
        "目标": "## 0. 一页纸摘要",
        "范围": "### 4.1 MVP 内",
        "验收标准": "## 7. 验收标准",
        "失败模式": "### 6.8 故障处理策略",
        "优先级": "### 1.4 优先级原则",
        "非目标": "### 4.3 明确不做",
    }
    for dim, marker in dims.items():
        check(f"维度已记录 {dim}", marker in text)

    # 3.5 第三轮新增：内置插件与实施路线
    check("内置插件套件已成节", "**内置套件（默认可用，优先级最高）**" in text)
    check("实施路线与估算已成节", "## 12. 实施路线与估算" in text)

    # 4. 验收编号 AC-00..AC-13，唯一且齐全
    acs = sorted(set(re.findall(r"AC-\d{2}", text)))
    expected = [f"AC-{i:02d}" for i in range(14)]
    check("AC-00..AC-13 齐全且唯一", acs == expected, ",".join(acs))

    # 5. 决策编号齐全
    nums = {int(m) for m in re.findall(r"^\| (\d{1,2}) \|", text, re.M)}
    check("决策编号 1..15 齐全", nums == set(range(1, 16)), str(sorted(nums)))

    # 6. 任务 Schema 是合法 JSON，且答案自洽
    block = re.search(r"```json\n(.*?)\n```", text, re.S)
    task = None
    if block:
        try:
            task = json.loads(block.group(1))
            check("任务 Schema 是合法 JSON", True)
        except json.JSONDecodeError as exc:
            check("任务 Schema 是合法 JSON", False, str(exc))
    else:
        check("任务 Schema 是合法 JSON", False, "未找到 ```json 代码块")

    if task:
        ans = task.get("answer", {})
        options = ans.get("options", [])
        idx = ans.get("correct_index", -1)
        picked = options[idx] if 0 <= idx < len(options) else None
        check("correct_index 指向正确选项", picked == "2.4 kHz", f"picked={picked}")
        check("Schema 版本号正确", task.get("schema") == "earlab.task/1")
        check("题目技能在技能图谱中", f"`{task.get('skill')}`" in text, str(task.get("skill")))
        check("题目含等响配置", task.get("loudness", {}).get("mode") == "itu_r128_integrated")
        check("题目含随机化配置", task.get("randomization", {}).get("ab_label") is True)

    # 7. 所有 Markdown 表格列数一致
    bad_tables = []
    i = 0
    while i < len(lines):
        if lines[i].startswith("|"):
            block_lines = []
            while i < len(lines) and lines[i].startswith("|"):
                block_lines.append(lines[i])
                i += 1
            widths = {len(l.strip().strip("|").split("|")) for l in block_lines}
            if len(widths) != 1:
                bad_tables.append((block_lines[0][:50], sorted(widths)))
        else:
            i += 1
    check("所有表格列数一致", not bad_tables, str(bad_tables[:3]))

    # 8. 不残留旧版内容
    stale = ["PRD-v0.1", "EQ 训练 5 种模式", "40–70 万", "70–100 万"]
    for s in stale:
        check(f"无旧版残留 {s}", s not in text)

    # 9. 预算算术一致
    parts = [16.0, 12.0, 2.2, 2.4, 2.0]
    check(
        "预算分项合计 34.6 万且区间 35–40 万",
        abs(sum(parts) - 34.6) < 1e-6 and "35–40 万" in text,
        f"sum={sum(parts)}",
    )

    # 10. 非目标逐条覆盖
    sec43 = text.split("### 4.3 明确不做")[1].split("---")[0]
    for kw in ["现场扩声", "DAW", "VST2", "云同步", "实时麦克风", "macOS", "移动端", "教师端"]:
        check(f"非目标覆盖 {kw}", kw in sec43)

    # 11. 技能图谱各 12 项且不重复
    eq = re.findall(r"^\| `eq\.([a-z_]+)`", text, re.M)
    dyn = re.findall(r"^\| `dyn\.([a-z_]+)`", text, re.M)
    check("EQ 技能 12 项且唯一", len(eq) == 12 and len(set(eq)) == 12, f"{len(eq)} 项")
    check("动态技能 12 项且唯一", len(dyn) == 12 and len(set(dyn)) == 12, f"{len(dyn)} 项")

    # 12. MVP 题型恰为 4 个（与 4.1 表述一致）
    sec52 = text.split("### 5.2 训练模式")[1].split("### 5.3")[0]
    mvp_rows = [l for l in sec52.splitlines() if l.startswith("|") and l.rstrip().endswith("| MVP |")]
    check("MVP 题型恰为 4 个", len(mvp_rows) == 4, f"{len(mvp_rows)} 个")

    # 13. 现场扩声技能已移出 MVP
    hunt = [l for l in text.splitlines() if "`eq.feedback_hunt`" in l]
    check("啸叫技能标记为 1.1", bool(hunt) and "1.1" in hunt[0], hunt[0][:80] if hunt else "未找到")

    # 14. 周期口径一致
    check("里程碑总周期 17 周", "合计约 17 周" in text)
    check("摘要周期 16–17 周", "16–17 周" in text)

    # 输出
    failed = [r for r in results if not r[1]]
    width = max(len(r[0]) for r in results) + 2
    for name, ok, detail in results:
        flag = "PASS" if ok else "FAIL"
        print(f"[{flag}] {name:<{width}} {detail}")
    print(f"\n{len(results) - len(failed)}/{len(results)} 项通过")
    if failed:
        print("\n失败项：")
        for name, _, detail in failed:
            print(f"  - {name} {detail}")
        return 1
    print("GREEN: 全部通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
