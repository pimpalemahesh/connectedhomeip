# TLV Diagnostic Data Parser

This tool is a graphical interface for parsing binary TLV diagnostic data files and displaying structured information such as metrics, traces, and counters.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Environment Setup](#environment-setup)
- [Running the Application](#running-the-application)
- [Usage](#usage)
- [Troubleshooting](#troubleshooting)

## Prerequisites

Python: Ensure you have Python installed.
Connected Home over IP (CHIP) Environment: This tool relies on the CHIP (Matter) libraries. You must have the CHIP repository cloned and built on your system.
Chip-Tool: You must have the latest chip-tool installed from the master branch.

## Installation

Clone this repository or download the files.

Install the required Python dependencies:

```
pip install -r requirements.txt
```
Verify that your environment has tkinter available (typically installed with Python). If you encounter issues with tkinter, install it based on your OS:

Debian/Ubuntu: sudo apt-get install python3-tk
macOS: Included with Python installation
Windows: Ensure tkinter is part of your Python installation.
Clone the connectedhomeip repository from https://github.com/project-chip/connectedhomeip.

Navigate to the connectedhomeip repository and build chip-tool:
```
cd connectedhomeip
./scripts/examples/gn_build_example.sh examples/chip-tool out/host
```

 ## Environment Setup

Set up the following environment variables for the script to locate the necessary CHIP libraries:

CHIP_HOME: Set this to the path of your CHIP (Matter) repository.

```
export CHIP_HOME=path/to/connectedhomeip/
```
Python Path Setup: Ensure the CHIP Python libraries are accessible:

```
export PYTHONPATH=$CHIP_HOME/src/controller/python:$PYTHONPATH
```

## Running the Application

Give the script executable permission:

```
chmod +x tlv_diagnostic_parser.py
```
Run the application:

```
./tlv_diagnostic_parser.py
```

## Usage

Enter Binary File Path: In the "Enter Binary File Path" field, specify the path to the binary file containing TLV-encoded data.

Click Convert: Click the Convert button to parse and display the data in the text area below.

The parsed data will be displayed in a structured table format under different sections for Metrics, Traces, and Counters.

### Troubleshooting

chip.tlv ImportError: If the tool reports an error finding chip.tlv, ensure CHIP_HOME is set correctly and PYTHONPATH is updated as mentioned in the Environment Setup section.

File Not Found Error: Make sure the path you enter is correct and points to a valid binary TLV file.

GUI Issues: If the GUI fails to load, make sure tkinter is properly installed and available in your environment.
