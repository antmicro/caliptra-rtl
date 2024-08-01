# SIteSpawner (SIS)

## Installation

```
pip3 install -e .
```

## Usage

All subcommands and accepted arguments can be printed with:

```
sis --help
```

The tool is consists of 3 individual subcommands, and an aggregate of them. 

### Coverage data conversion

`*.dat` coverage data into `*.info` files conversion is executed via:

```
sis convert
```

The tool allows to pass a path to the directory containing `*.dat` files via `--dat-dir` option. If not specified, the current working directory will be considered.

Similarly, it allows to specify an output directory for produced `*.info` files via `--info-dir`. If not specified, the `*.info` files will be stored where their `*.dat` counterparts are. 

### Coverage dashboard generation

Coverage dashboard from `*.info` files can be generated with:

```
sis reports
```

### Webpage assembly (update)

Collect coverage dashboard (optionally documentation) and merge it into existing collection of pages:

```
sis webpage --loc-github-ref-name <ref>
            --loc-github-event-name <event> --pr-number <pr_no>
```

Command expects reference name, event name and PR number if applies.
Those parameters dictate the localization of the generated pages in the website.

E.g. if `ref` is `main`, the coverage dashboard and documentation will be placed under `BASE_URL/main/...`. 

Similarly, if pages where generated within a merge request of number `<no>`, the pages will be located under `BASE_URL/dev/<no>/...`