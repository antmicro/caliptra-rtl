"""
This script generates HTML-based reports from lcov summary tables.

To use the script, first generate the summary table into a file, e.g.
`lcov -l example.info > example.summary`

Then, run:
`python3 genhtml.py example.summary --output-dir report/`
Note that multiple summary files can be provided and the report will render
them side-by-side

Full usage help:
usage: genhtml.py [-h] [--output-dir output_directory] [--test-name test_name] [--logo-src logo_src]
                  [--logo-href logo_href] input_files [input_files ...]

Generate HTML-based coverage reports from "lcov --list-full-path -l" summaries of info files.

positional arguments:
  input_files           Paths to summary files

options:
  -h, --help            show this help message and exit
  --output-dir output_directory
                        Path to output directory (default:
                        report/)
  --test-name test_name
                        Test name to be displayed in the report
  --logo-src logo_src   Path to logo to be attached with the report, relative to index.html
                        file in the destination dir.
  --logo-href logo_href
                        URL to be associated with the logo.

"""

import argparse
import datetime
from collections import OrderedDict, defaultdict
from copy import deepcopy
from pathlib import Path
from os import path

from jinja2 import Environment, FileSystemLoader


def get_color(value: float, min_value: float = 0, max_value: float = 100):
    if isinstance(value, str):
        value = float(value.strip(" ").strip("%"))
    midpoint = (max_value - min_value) / 2
    if value <= midpoint:
        r, g, b = (255, int(255 * value / midpoint), 0)
    else:
        r, g, b = (int(255 * (max_value - value) / midpoint), 255, 0)
    return "#%s%s%s;" % tuple([hex(c)[2:].rjust(2, "0") for c in (r, g, b)])


def process_line(line, data, filename):
    segments = line.split("|")
    src_path, cov, _, _ = segments
    src_file_name = filename.split("/")[-1]
    module_name, _ = src_file_name.split("_")[-1].split(".")
    data[src_path.strip()][module_name] = [i for i in cov.split(" ") if i]


def generate_table(data, template_env, links=False):
    cov_types = list(list(data.items())[0][1].keys())
    num_tests = len(cov_types)
    raw_widths = [40, 20, 20]
    widths_arr = [str(i / num_tests) + "%" for i in raw_widths]

    name_w = 20
    cov_container_size = (100 - name_w) / num_tests
    hit_w = cov_container_size / 4
    rate_w = cov_container_size - hit_w

    template_env.globals["get_color"] = get_color
    main_table_html = template_env.get_template("main_table.html")
    # Only pass actual coverage data, leave out the summary
    cov_data = {k: v for (k, v) in data.items() if k != "Total:"}
    return main_table_html.render(
        cov_types=cov_types,
        width_cov_desc=sum(raw_widths) / num_tests,
        name_w=name_w,
        rate_w=rate_w,
        hit_w=hit_w,
        data=cov_data,
        widths_arr=widths_arr,
        links=links,
    )


def generate_summary(data: list, key: str, template_env: Environment, new_row=False):
    summary_html = template_env.get_template("summary_table.html")

    full_cov_color = get_color(float(data[0].strip("%")), data[1])
    return summary_html.render(
        new_row=new_row,
        cov_type_token=key,
        color_token=full_cov_color,
        hitrate_token=data[0],
        hit_token=str(int(int(data[1]) * (float(data[0].replace("%", "")) / 100))),
        total_token=data[1],
    )


def render_page(data, view, out_dir, test_name, logo_src, logo_href, template_env, links=False):
    report_html = template_env.get_template("coverage_report.html")

    output = report_html.render(
        header_token="Full",
        logo_src=logo_src,
        logo_href=logo_href,
        fulltable_token=generate_table(data, template_env, links),
        view_token=view,
        testname_token=test_name,
        time_token=datetime.datetime.now().strftime("%d-%m-%Y"),
        **{
            f"{test}_summary_token": generate_summary(data["Total:"][test], test, template_env)
            for test in data["Total:"].keys()
        },
    )

    with open(out_dir, "w") as f:
        print(output, file=f)


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


def main(args):
    input_files = args.summary_files
    output_dir = args.output_dir
    test_name = args.test_name
    data = defaultdict(lambda: defaultdict(list))
    code_root_path = None
    for i in input_files:
        with open(i, "r") as f:
            for line in f:
                if line.strip().startswith(("|", "=", "Filename", "Reading tracefile")):
                    continue
                elif line.startswith("Message summary"):
                    break
                elif line.startswith("["):
                    code_root_path = line.strip(" []\n")
                else:
                    process_line(line, data, i)

    # The LCOV must be ran with '--list-full-path' so that the paths to sources
    # are not 'simplified' with '...'.
    # Find longest common path for all sources
    # Skip 'Total: (...)' and similar lines
    code_root_path = path.commonpath([x for x in data.keys() if Path(x).is_absolute()])
    code_root_path = Path(code_root_path).parent

    data = unify_dict(data)
    tld = generate_dir_dict(data, code_root_path)

    self_dir = path.dirname(path.realpath(__file__))
    template_env = Environment(
        loader=FileSystemLoader(path.join(self_dir, "source.template", "coverage_report"))
    )

    for key in list(tld.keys()):
        if key == "Total:":
            continue
        subdata = generate_file_dict(data, Path(key), code_root_path)
        render_page(
            subdata,
            "<a href=index.html>top level</a> - " + " - ".join(key.split("/")),
            f"{output_dir}/index_{key.replace('/','_')}.html",
            test_name,
            args.logo_src,
            args.logo_href,
            template_env,
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
    render_page(
        tld,
        "top level",
        f"{output_dir}/index.html",
        test_name,
        args.logo_src,
        args.logo_href,
        template_env,
        True,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description=(
            "Generate HTML-based coverage reports from "
            '"lcov --list-full-path -l" summaries of info files.'
        )
    )
    parser.add_argument(
        "summary_files",
        metavar="input_files",
        type=str,
        nargs="+",
        help="Paths to summary files",
    )
    parser.add_argument(
        "--output-dir",
        metavar="output_directory",
        type=str,
        default="report",
        help="Path to output directory (default: report/)",
    )
    parser.add_argument(
        "--test-name",
        metavar="test_name",
        type=str,
        default="all",
        help="Test name to be displayed in the report",
    )

    parser.add_argument(
        "--logo-src",
        metavar="logo_src",
        default="_static/white.svg",
        type=str,
        help="Path to logo to be attached with the report, relative to index.html file in the destination dir.",
    )

    parser.add_argument(
        "--logo-href",
        metavar="logo_href",
        default="index.html",
        type=str,
        help="URL to be associated with the logo.",
    )
    args = parser.parse_args()

    input_files = args.summary_files
    output_dir = args.output_dir

    for file in input_files:
        if not path.isfile(file):
            print(f"Error: Input file '{file}' does not exist.")
            input_files.remove(file)

    if not path.isdir(output_dir):
        print(f"Error: Output directory '{output_dir}' does not exist.")
        exit(1)

    main(args)
