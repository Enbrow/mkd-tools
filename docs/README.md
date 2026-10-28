# mkd documentation

Read, search and create Monokakido dictionaries with Python or C++.

## Installation

### Python library

Install mkd-tools from PyPI into your Python
environment:

```sh
python -m pip install mkd-tools
```

Import the library as `mkd`:

```python
import mkd
```

For a complete creation example, see [Birb dictionaries](https://github.com/kiwakiwaa/mkd-tools/tree/main/examples/birbs).
It includes a general dictionary, a kanwa dictionary and a field guide, with
article XML, stylesheets and media.

### CLI

On macOS, install the CLI with [Homebrew](https://brew.sh/):

```sh
brew install kiwakiwaa/tap/mkd-tools
```

For the C++ library or a local build, see [Build from source](setup/building.md).

## CLI usage

List products, inspect metadata and export resources from the terminal.

```sh
mkd-tools list
mkd-tools info DAIJISEN2
mkd-tools export DAIJISEN2
```

On macOS, installed dictionaries are found automatically. Elsewhere, provide the directory containing product folders:

```sh
mkd-tools -d /path/to/dictionaries list
mkd-tools -d /path/to/dictionaries export DAIJISEN2
```

Exports go to `./mkd-export/<product-id>` by default. Use `-o` to select an output
directory.

```sh
mkd-tools export DAIJISEN2 -o ./output
mkd-tools export DAIJISEN2 --format tar -o ./output
mkd-tools export DAIJISEN2 --only entries --pretty
mkd-tools export DAIJISEN2 --only graphics --only audio
mkd-tools export DAIJISEN2 --only keystores --keystore-mode both
```

`--only` accepts `entries`, `graphics`, `audio`, `fonts`, `headlines`, `keystores`
and `appendix-entries`.
`--pretty` formats XML.
Use `mkd-tools --help` or `mkd-tools export --help` for all options.

For custom products, see [installation and replacement](authoring/publishing.md#install-and-replace).

## Library guides

Start with [products, dictionaries and articles](concepts.md), then
[read a dictionary](reading/products.md), [search](searching/queries.md) or
[create your first dictionary](authoring/first_dictionary.md).
