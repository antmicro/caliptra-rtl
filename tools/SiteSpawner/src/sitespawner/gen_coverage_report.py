import subprocess
import sys
from pathlib import Path
from shutil import copy2

from .common import args_on_debug_logger, main_func_log, setup_logger, styles_dir
from .genhtml import genhtml, get_common_src_path, parse_summaries

logger = setup_logger(name=__name__, stream=sys.stdout)


def lcov_summary(input_file, output_file):
    """Invokes lcov tool to generate summary out of `input_file`."""
    subprocess.run(
        ["lcov", "--list-full-path", "-l", input_file],
        stdout=open(output_file, "w+"),
    )


def lcov_merge(input_file, output_file):
    """Invokes lcov tool to add `input_file` into the tracefile.
    `output_file` becomes then an aggregate of *.info files."""
    subprocess.run(
        ["lcov", "-a", input_file, "-o", output_file],
        stdout=open(f"{input_file}_merge.log", "w+"),
    )


@args_on_debug_logger(logger=logger)
def lcov_genhtml(info_files, path_prefix, src_dir, lcov_report_dir="lcov_report"):
    """Invokes lcov's genhtml tool to generate source file views for the coverage report."""
    # If `path_prefix` is not available, link it to the existing project directory
    # so that the source files can be fetched for the report
    if not src_dir:
        logger.warning(f"Sources directory not specified, using {path_prefix}")
        src_dir = path_prefix

    if not Path(src_dir).exists():
        raise ValueError(f"Sources path doesn't exist {src_dir}")

    src_dir = Path(src_dir).resolve()

    # Align paths to end in the same directory:
    parts = path_prefix.parts
    src_dir_parts = str(src_dir).split("/")
    for i in reversed(range(len(parts))):
        path_prefix = path_prefix.parent
        if parts[i] == src_dir_parts[-1]:
            break

    path_prefix = Path(path_prefix)

    logger.debug(f"Deduced source path prefix: {path_prefix}")

    if path_prefix is not None and not path_prefix.exists():
        logger.debug(f"Creating symlink: {path_prefix} -> {src_dir}")
        path_prefix.parent.mkdir(parents=True, exist_ok=True)
        path_prefix.symlink_to(Path(src_dir).parent, target_is_directory=True)

    # TODO: FIX LOG PATH
    if not path_prefix:
        subprocess.run(
            ["genhtml", "--output-dir", lcov_report_dir, *info_files],
            stdout=open("lcov_genhtml.log", "w+"),
        )
    else:
        subprocess.run(
            ["genhtml", "--output-dir", lcov_report_dir, "--prefix", str(path_prefix), *info_files],
            stdout=open("lcov_genhtml.log", "w+"),
        )


@args_on_debug_logger(logger=logger)
def generate_coverage_reports(
    output_dir,
    src_path=None,
    src_pattern="*",
    src_remove_pattern=None,
    logo_src=None,
    logo_href=None,
    info_report_dir=None,
):
    """Iterates over available *.info files, merges them & generates summaries
    for each coverage type with the use of lcov.
    Calls `genhtml` to generate coverage dashboards for individual tests as
    well as for the all tests combined."""
    curr_dir = Path.cwd()
    if not info_report_dir:
        info_report_dir = curr_dir

    # Extract coverage info files
    info_files = Path(info_report_dir).glob("**/*.info")
    for info_file in info_files:
        subprocess.run(
            ["lcov", "--extract", info_file, src_pattern, "-o", info_file],
            stdout=open(f"{info_file}_extraction.log", "w+"),
        )
        if src_remove_pattern is not None:
            subprocess.run(
                ["lcov", "--remove", info_file, *src_remove_pattern, "-o", info_file],
                stdout=open(f"{info_file}_remove.log", "w+"),
            )

    # Run LCOV's genhtml to gather source-file pages
    branch_merged = Path("./merged_branch.info")
    toggle_merged = Path("./merged_toggle.info")
    branch_merged_summary = Path("./merged_branch.summary")
    toggle_merged_summary = Path("./merged_toggle.summary")

    # Find and classify coverage files
    branch_files, toggle_files = {}, {}
    files = Path(info_report_dir).glob("**/coverage_*.info")

    for file in files:
        if file.name.endswith("_branch.info"):
            branch_files[file.name.removesuffix("_branch.info")] = file
        elif file.name.endswith("_toggle.info"):
            toggle_files[file.name.removesuffix("_toggle.info")] = file

    # Generate reports for each coverage file set
    for name_body in branch_files.keys():
        branch_file = branch_files[name_body]
        toggle_file = toggle_files[name_body]
        test_name = name_body.removeprefix("coverage_")

        if branch_file and Path(branch_file).exists():
            lcov_summary(branch_file, f"{name_body}_branch.summary")

        if toggle_file and Path(toggle_file).exists():
            lcov_summary(toggle_file, f"{name_body}_toggle.summary")

        test_output_dir = Path(output_dir) / f"all_{test_name}"
        (test_output_dir / "_static").mkdir(parents=True, exist_ok=True)

        info_files = Path(info_report_dir).glob(f"**/*{test_name}*.info")
        lcov_html_dir = curr_dir / "lcov_report"

        # TODO: Not optimal, same invoked in 'genhtml.py'
        data, _ = parse_summaries([f"{name_body}_toggle.summary", f"{name_body}_branch.summary"])
        path_prefix = get_common_src_path(data.keys())
        lcov_genhtml(info_files, path_prefix, src_path, lcov_html_dir)
        genhtml(
            input_files=[f"{name_body}_toggle.summary", f"{name_body}_branch.summary"],
            output_dir=test_output_dir,
            test_name=test_name,
            logo_src=logo_src,
            logo_href=logo_href,
            html_src_dir=lcov_html_dir,
        )

        copy2(styles_dir / "main.css", test_output_dir)
        copy2(styles_dir / "cov.css", test_output_dir)
        copy2(
            styles_dir / "assets" / "chips-alliance-logo-mono.svg",
            test_output_dir / "_static" / "white.svg",
        )

    # Merge branch files
    for branch_file in branch_files.values():
        lcov_merge(branch_file, branch_merged)

    if branch_merged.exists():
        lcov_summary(branch_merged, branch_merged_summary)

    # Merge toggle files
    for toggle_file in toggle_files.values():
        lcov_merge(toggle_file, toggle_merged)

    if toggle_merged.exists():
        lcov_summary(toggle_merged, toggle_merged_summary)

    # Generate final combined report
    final_output_dir = Path(output_dir) / "all"
    (final_output_dir / "_static").mkdir(parents=True, exist_ok=True)

    # TODO: Not optimal, same invoked in 'genhtml.py'
    data, _ = parse_summaries([str(toggle_merged_summary), str(branch_merged_summary)])
    path_prefix = get_common_src_path(data.keys())
    lcov_genhtml([branch_merged, toggle_merged], path_prefix, src_path, lcov_html_dir)
    genhtml(
        input_files=[str(toggle_merged_summary), str(branch_merged_summary)],
        output_dir=final_output_dir,
        test_name="all",
        logo_src=logo_src,
        logo_href=logo_href,
        html_src_dir=lcov_html_dir,
    )

    copy2(styles_dir / "main.css", final_output_dir)
    copy2(styles_dir / "cov.css", final_output_dir)
    copy2(
        styles_dir / "assets" / "chips-alliance-logo-mono.svg",
        final_output_dir / "_static" / "white.svg",
    )


@main_func_log(logger, "Generate Coverage Reports")
def main(args):
    # Set output directory and create it if it doesn't exist
    report_dir = Path(args.report_dir)
    report_dir.mkdir(parents=True, exist_ok=True)

    generate_coverage_reports(
        output_dir=report_dir,
        src_pattern=args.src_pattern,
        src_remove_pattern=args.src_remove_pattern,
        src_path=args.src_path,
        logo_src=args.logo_src,
        logo_href=args.logo_href,
        info_report_dir=args.info_report_dir,
    )
