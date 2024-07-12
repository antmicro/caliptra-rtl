"""
This script generates HTML-based reports from lcov summary tables.

To use the script, first generate the summary table into a file, e.g.
`lcov -l example.info > example.summary`

Then, run:
`python3 genhtml.py example.summary --output-dir report/`
Note that multiple summary files can be provided and the report will render
them side-by-side

Full usage help:
usage: genhtml.py [-h] [--output-dir output_directory]
                  input_files [input_files ...]

Generate HTML-based coverage reports from "lcov --list-full-path -l" summaries of info files.

positional arguments:
  input_files           Paths to summary files

options:
  -h, --help            show this help message and exit
  --output-dir output_directory
                        Path to output directory (default:
                        report/)
"""

import argparse
import os
from collections import OrderedDict, defaultdict
from copy import deepcopy
from pathlib import Path

REPORT_TEMPLATE = """<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">

<html lang="en">

<head>
  <meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
  <title>LCOV Custom Render</title>
  <link rel="stylesheet" type="text/css" href="gcov.css">
  <style>
        .container {
            background-color: rgb(169,169,169);
            width: 100%;
            min-height: 80%;
            border-radius: 15px;
        }

    </style>
</head>

<body>

          <table width="100%" border=0 cellspacing=0 cellpadding=0>
            <tr><td class="title">RTL <easily_replacable_token_1> coverage report</td></tr>
            <tr><td class="ruler"><img src="glass.png" width=3 height=3 alt=""></td></tr>

            <tr>
              <td width="100%">
                <table cellpadding=1 border=0 width="100%">
          <tr>
            <td width="10%" class="headerItem">Current view:</td>
            <td width="10%" class="headerValue"><easily_replacable_token_7></td>
            <td width="5%"></td>
            <td width="5%"></td>
            <td width="5%" class="headerCovTableHead">Coverage</td>
            <td width="5%" class="headerCovTableHead" title="Covered + Uncovered code">Hit</td>
            <td width="5%" class="headerCovTableHead" title="Exercised code only">Total</td>
          </tr>
          <tr>
            <td class="headerItem">Test:</td>
            <td class="headerValue"><easily_replacable_token_8></td>
            <td></td>
            <test_token1>
          </tr>
          <tr>
            <td class="headerItem">Test Date:</td>
            <td class="headerValue"><easily_replacable_token_5></td>
            <td></td>
            <test_token2>
          </tr>
          <test_token3>
                  <tr><td><img src="glass.png" width=3 height=3 alt=""></td></tr>
                </table>
              </td>
            </tr>

            <tr><td class="ruler"><img src="glass.png" width=3 height=3 alt=""></td></tr>
          </table>

          <center>
              <easily_replacable_token_6>
          </center>
          <br>
          <br>
</body>
</html>
"""


def get_color(value: float, min_value: float = 0, max_value: float = 100):
    midpoint = (max_value - min_value) / 2
    if value <= midpoint:
        return (255, int(255 * value / midpoint), 0)
    return (int(255 * (max_value - value) / midpoint), 255, 0)


def process_line(line, data, filename):
    segments = line.split("|")
    src_path, cov, _, _ = segments
    src_file_name = filename.split("/")[-1]
    module_name, _ = src_file_name.split("_")[-1].split(".")
    data[src_path.strip()][module_name] = [i for i in cov.split(" ") if i]


def generate_table(data, links=False):
    return """
<table width="80%" cellpadding=1 cellspacing=1 border=0>
<tr>
<td width="40%">Directory</td>
<td width="20%">Rate</td>
<td width="20%"></td>
<td width="20%">Hit / Total</td>
</tr>

<token1>

</table>
        """.replace(
        "<token1>", generate_table_tokenstr(data, links)
    )


def generate_table_tokenstr(data, links=False):
    ddata = dict(data)
    token_str = ""

    num_tests = len(list(list(ddata.items())[0][1].items()))
    raw_widths = [40, 20, 20]
    widths_arr = [str(i / num_tests) + "%" for i in raw_widths]
    token_str += '<tr><td width=20% style="border-top: 0px; border-left: 0px;"></td>'
    for key in list(list(ddata.items())[0][1].keys()):
        token_str += '<td style="text-align: center;" width='
        token_str += str(sum(raw_widths) / num_tests) + "%"
        token_str += " colspan = 3>"
        token_str += key[0].upper() + key[1:]
        token_str += "</td>"
    token_str += "</tr>"

    for file, cov_data in ddata.items():
        if file == "Total:":
            continue

        token_str += "<tr>"
        token_str += "<td width=20%>"
        if links:
            token_str += "<a href=index_"
            token_str += file.replace("/", "_")
            token_str += ".html>"
            token_str += file
            token_str += "</a>"
        else:
            token_str += file
        token_str += "</td>"

        for key, numbers in cov_data.items():
            r, g, b = get_color(float(numbers[0].replace("%", "")))
            cov_color = "#%s%s%s;" % tuple([hex(c)[2:].rjust(2, "0") for c in (r, g, b)])

            token_str += "<td width="
            token_str += widths_arr[0]
            token_str += ">"
            token_str += '<div class="container">'
            token_str += '<div style="border-radius: 15px; height: 80%; background-color: '
            token_str += cov_color
            token_str += "width: "
            if float(numbers[0].replace("%", "")) > 5:
                token_str += numbers[0]
            else:
                token_str += "5%"
            token_str += ';">&nbsp</div></div></td>'

            token_str += "<td width="
            token_str += widths_arr[1]
            token_str += ' style="text-align: center; color: '
            token_str += cov_color
            token_str += ';">'
            token_str += numbers[0]
            token_str += "</td>"

            token_str += "<td width="
            token_str += widths_arr[2]
            token_str += ' style="text-align: center;">'
            token_str += str(int(int(numbers[1]) * (float(numbers[0].replace("%", "")) / 100)))
            token_str += " / "
            token_str += numbers[1]
            token_str += "</td>"
        token_str += "</tr>"
    return token_str


SUMMARY_TEMPLATE = """
<td class="headerItem"><token1></td>
<td class="headerCovTableEntry" style="color: #0E1116; background-color: <token2>">
    <token3>
</td>
<td class="headerCovTableEntry"><token4></td>
<td class="headerCovTableEntry"><token5></td>
"""


def format_key(key: str):
    return key[0].upper() + key[1:] + ":"


def generate_summary(data: list, key: str, new_row=False):
    r, g, b = get_color(float(data[0].strip("%")))
    full_cov_color = "#%s%s%s;" % tuple([hex(c)[2:].rjust(2, "0") for c in (r, g, b)])

    inner_row = (
        SUMMARY_TEMPLATE.replace("<token1>", format_key(key))
        .replace("<token2>", full_cov_color)
        .replace("<token3>", data[0])
        .replace("<token4>", str(int(int(data[1]) * (float(data[0].replace("%", "")) / 100))))
        .replace("<token5>", data[1])
    )

    if new_row:
        return "<tr><td></td><td></td><td></td>" + inner_row + "</tr>"

    return inner_row


def render_page(data, view, out_dir, links=False):
    report_html = deepcopy(REPORT_TEMPLATE)
    report_html = report_html.replace("<easily_replacable_token_1>", "Full")
    for it, test in enumerate(data["Total:"].keys()):
        tok = "<test_tokenX>".replace("X", str(it + 1))
        report_html = report_html.replace(tok, generate_summary(data["Total:"][test], test))
    report_html = report_html.replace("<easily_replacable_token_6>", generate_table(data, links))
    report_html = report_html.replace("<easily_replacable_token_7>", view)
    with open(out_dir, "w") as f:
        print(report_html, file=f)


def generate_dir_dict(data, dir):
    gdict = defaultdict(lambda: defaultdict(list))
    for file, cov_data in dict(data).items():
        if file == "Total:":
            gdict[file] = deepcopy(cov_data)
            continue

        base = Path(file).parent.relative_to(dir)

        for key, d in cov_data.items():
            gdict[str(base)][key].append(d)

    return OrderedDict(sorted(gdict.items()))


def generate_file_dict(data, base: Path, code_root_path: Path):
    gdict = defaultdict(lambda: defaultdict(list))
    hit, total = defaultdict(int), defaultdict(int)
    for file, cov_data in dict(data).items():
        if file == "Total:":
            continue
        if Path(file).parent.relative_to(code_root_path) == base:
            for key, data in cov_data.items():
                gdict[Path(file).name][key] = data
                frac = float(data[0].strip("%")) / 100
                hit[key] += int(frac * int(data[1]))
                total[key] += int(data[1])
        for key, data in cov_data.items():
            if total[key] > 0:
                gdict["Total:"][key] = [
                    "{:.1f}%".format(hit[key] / total[key] * 100),
                    str(total[key]),
                ]
            else:
                gdict["Total:"][key] = ["0%", "0"]
    return gdict


def unify_dict(data):
    # Figure out the list of all tests:
    tests = set()
    for _, cov_data in data.items():
        for key in cov_data.keys():
            tests.add(key)
    # Add missing ones to the dict:
    for key, cov_data in data.items():
        for test in tests:
            if test not in cov_data:
                cov_data[test] = ["0.0%", "0"]
    return data


def main(input_files, output_dir):
    data = defaultdict(lambda: defaultdict(list))
    code_root_path = None
    for i in input_files:
        with open(i, "r") as f:
            for line in f:
                if line.strip().startswith(("|", "=", "Filename", "Reading tracefile")):
                    continue
                elif line.startswith("Message summary"):
                    break
                else:
                    process_line(line, data, i)

    # The LCOV must be ran with '--list-full-path' so that the paths to sources
    # are not 'simplified' with '...'.
    # Find longest common path for all sources
    # Skip 'Total: (...)' and similar lines
    code_root_path = os.path.commonpath([x for x in data.keys() if Path(x).is_absolute()])
    code_root_path = Path(code_root_path).parent

    data = unify_dict(data)
    tld = generate_dir_dict(data, code_root_path)

    for key in list(tld.keys()):
        if key == "Total:":
            continue
        subdata = generate_file_dict(data, Path(key), code_root_path)
        render_page(
            subdata,
            "<a href=index.html>top level</a> - " + " - ".join(key.split("/")),
            f"{output_dir}/index_{key.replace('/','_')}.html",
        )

    for file, cov_data in tld.items():
        if file == "Total:":
            continue
        for test_type, dat in cov_data.items():
            hit, total = 0, 0
            for measurement in dat:
                frac = float(measurement[0].strip("%")) / 100
                hit += int(frac * int(measurement[1]))
                total += int(measurement[1])
            if total > 0:
                cov_data[test_type] = ["{:.1f}%".format(hit / total * 100), str(total)]
            else:
                cov_data[test_type] = ["0%", "0"]
    render_page(tld, "top level", f"{output_dir}/index.html", True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description=(
            "Generate HTML-based coverage reports from "
            '"lcov --list-full-path -l" summaries of info files.'
        )
    )
    parser.add_argument(
        "summary_files", metavar="input_files", type=str, nargs="+", help="Paths to summary files"
    )
    parser.add_argument(
        "--output-dir",
        metavar="output_directory",
        type=str,
        default="report",
        help="Path to output directory (default: report/)",
    )

    args = parser.parse_args()

    input_files = args.summary_files
    output_dir = args.output_dir

    for file in input_files:
        if not os.path.isfile(file):
            print(f"Error: Input file '{file}' does not exist.")
            input_files.remove(file)

    if not os.path.isdir(output_dir):
        print(f"Error: Output directory '{output_dir}' does not exist.")
        exit(1)

    main(input_files, output_dir)
