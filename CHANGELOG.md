# Change log

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

## [Unlreleased]

## [v0.2.0] 2025-12-28

Library features:
* Improve the performance of merging drivers by avoiding cloning interval maps and adding a special case for matching ranges.
* Improve labeling of the bit ranges on netlist edges.
* Add basic combinational loop detection.

Driver features:
* Add options to report variables, ports and registers to stdout, formatted as tabular data.

Python bindings:
* Add binding to iterate over nodes in the netlist.

Bug fixes:
* Handling of driver overlap cases.

## [v0.1.0] 2025-11-22

This is the initial release of slang netlist. It represents an early milestone
in the project since it was separated from upstream slang and mostly rewritten
on the way, but includes a baseline set of features, infrastructure and
documentation.
