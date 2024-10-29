# TLV Diagnostic Data Parser

This tool is a graphical interface for parsing binary TLV diagnostic data files and displaying structured information such as metrics, traces, and counters.

## Prerequisites

- Python: Ensure you have Python installed.
- Connected Home over IP (CHIP) Environment: This tool relies on the CHIP (Matter) libraries. You must have the CHIP repository cloned and built on your system.

 ## Environment Setup

Set up the following environment variables for the script to locate the necessary CHIP libraries:

CHIP_HOME: Set this to the path of your CHIP (Matter) repository.

```
export CHIP_HOME=path/to/connectedhomeip/
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

1. Click Refresh: Click the Refresh button to parse and display the data in the text area below. It will read data from particular log file which will be updated each time you run retrieve-logs command on chip-tool

2. The parsed data will be displayed in a structured table format under different sections for Metrics, Traces, and Counters.

### Troubleshooting

1. chip.tlv ImportError: If the tool reports an error finding chip.tlv, ensure CHIP_HOME is set correctly and PYTHONPATH is updated as mentioned in the Environment Setup section.

2. File Not Found Error: Make sure the path you enter is correct and points to a valid binary TLV file.

3. GUI Issues: If the GUI fails to load, make sure tkinter is properly installed and available in your environment.
