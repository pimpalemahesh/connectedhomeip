#!/usr/bin/env python3

import os
import sys
from enum import IntEnum
from tkinter import Tk, Button, Label, Text, Scrollbar, Frame, Entry, BOTH, END
from tabulate import tabulate  # Ensure to install via pip: `pip install tabulate`

# Import chip.tlv only if you have the proper module structure
try:
    sys.path.insert(0, 'src/controller/python')
    from chip.tlv import TLVReader
except ImportError:
    print("chip.tlv module not found. Ensure you are in the connectedhomeip/. directory.")
    sys.exit(1)

# TAG Enum for easier readability of data
class TAG(IntEnum):
    METRIC     = 0,
    TRACE      = 1,
    COUNTER    = 2,
    LABEL      = 3,
    SCOPE      = 4,
    VALUE      = 5,
    TIMESTAMP  = 6

# Lists to store parsed data
traces = []
metrics = []
counters = []

# Function to parse data from a hex string
def parse_data(file_path):
    global traces, metrics, counters
    traces, metrics, counters = [], [], []
    
    try:
        with open(file_path, 'rb') as file:
            binary_data = file.read()
        
        # Convert binary data to hex string for TLVReader
        hex_data = binary_data.hex()
        t = TLVReader(bytes.fromhex(hex_data))
        data = t.get()['Any']
        
        for i, j in data:
            if TAG(i) == TAG.METRIC:
                metrics.append(j)
            elif TAG(i) == TAG.TRACE:
                traces.append(j)
            elif TAG(i) == TAG.COUNTER:
                counters.append(j)
    
    except FileNotFoundError:
        output_text.insert(END, f"Error: File '{file_path}' not found.\n")
    except ValueError:
        output_text.insert(END, "Error: Invalid binary data in file.\n")

# Function to format data into table format
def get_table_data(diagnostic_name, dlist):
    table_data = []
    for diagnostic in dlist:
        row = []
        for key, value in diagnostic.items():
            tag_name = TAG(key).name.title()
            row.append((tag_name, value))
        table_data.append(row)
    return table_data

# Function to display data in the text widget
def display_data(diagnostic_name, dlist):
    if dlist:
        output_text.insert(END, f"\n{'*' * 25} {diagnostic_name} {'*' * 25}\n")
        
        table_data = get_table_data(diagnostic_name, dlist)
        formatted_data = []

        for row in table_data:
            formatted_row = []
            for tag, value in row:
                formatted_row.append(f"{tag}: {value}")
            formatted_data.append(formatted_row)

        output_text.insert(END, tabulate(formatted_data, headers=["Column 1", "Column 2", "Column 3"], tablefmt="fancy_grid"))
        output_text.insert(END, f"\n{'*' * 70}\n")
    else:
        output_text.insert(END, f"No {diagnostic_name} data available.\n")

# Callback for the Convert button
def convert_data():
    file_path = file_path_entry.get().strip()
    output_text.delete(1.0, END)
    parse_data(file_path)
    
    if traces:
        display_data("TRACES", traces)
    if metrics:
        display_data("METRICS", metrics)
    if counters:
        display_data("COUNTERS", counters)

# Main GUI application setup
root = Tk()
root.title("Hex Data Diagnostic Parser")
root.geometry("800x600")
root.configure(bg="#282C34")

# Create and place widgets
title_label = Label(root, text="Hex Data Diagnostic Parser", font=("Arial", 18, "bold"), fg="#61AFEF", bg="#282C34")
title_label.pack(pady=10)

file_path_label = Label(root, text="Enter Binary File Path:", font=("Arial", 12), fg="white", bg="#282C34")
file_path_label.pack(pady=5)

file_path_entry = Entry(root, font=("Arial", 12), width=60)
file_path_entry.pack(pady=5)

convert_button = Button(root, text="Convert", command=convert_data, font=("Arial", 12), bg="#98C379", fg="white", width=15)
convert_button.pack(pady=10)

# Frame for output with scrollbar
output_frame = Frame(root)
output_frame.pack(pady=10, fill=BOTH, expand=True)

scrollbar = Scrollbar(output_frame)
scrollbar.pack(side="right", fill="y")

output_text = Text(output_frame, wrap="word", yscrollcommand=scrollbar.set, font=("Courier New", 10), bg="#ABB2BF", fg="#282C34")
output_text.pack(fill=BOTH, expand=True)

scrollbar.config(command=output_text.yview)

# Start the main event loop
root.mainloop()
