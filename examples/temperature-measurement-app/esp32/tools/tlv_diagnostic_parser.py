#!/usr/bin/env python3

import os
import sys
from enum import IntEnum
from tkinter import Tk, Button, Label, Text, Scrollbar, Frame, Entry, BOTH, END
from tabulate import tabulate

# Set up paths from environment variables
CHIP_HOME = os.getenv("CHIP_HOME")

if not CHIP_HOME:
    print("Error: Please set the CHIP_HOME environment variables.")
    sys.exit(1)

# Update sys.path for TLVReader import based on CHIP_HOME
sys.path.insert(0, os.path.join(CHIP_HOME, 'src/controller/python'))
try:
    from chip.tlv import TLVReader
except ImportError:
    print("chip.tlv module not found. Ensure CHIP_HOME is set correctly.")
    sys.exit(1)

class TAG(IntEnum):
    METRIC = 0
    TRACE = 1
    COUNTER = 2
    LABEL = 3
    SCOPE = 4
    VALUE = 5
    TIMESTAMP = 6

traces, metrics, counters = [], [], []

def parse_data(file_path):
    global traces, metrics, counters
    traces, metrics, counters = [], [], []
    
    try:
        with open(file_path, 'rb') as file:
            binary_data = file.read()
        t = TLVReader(binary_data)
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

def get_table_data(diagnostic_name, dlist):
    table_data = []
    for diagnostic in dlist:
        if diagnostic_name == "TRACES":
            row = {
                "Timestamp": f"{diagnostic.get(TAG.TIMESTAMP, 0):.0f}",
                "Scope": diagnostic.get(TAG.SCOPE, ""),
                "Label": diagnostic.get(TAG.LABEL, "")
            }
        elif diagnostic_name == "METRICS":
            row = {
                "Timestamp": f"{diagnostic.get(TAG.TIMESTAMP, 0):.0f}",
                "Label": diagnostic.get(TAG.LABEL, ""),
                "Value": diagnostic.get(TAG.VALUE, "")
            }
        elif diagnostic_name == "COUNTERS":
            row = {
                "Timestamp": f"{diagnostic.get(TAG.TIMESTAMP, 0):.0f}",
                "Label": diagnostic.get(TAG.LABEL, ""),
                "Count": diagnostic.get(TAG.COUNTER, "")
            }
        table_data.append(row)
    return table_data

def display_data(diagnostic_name, dlist):
    output_text.tag_configure("center", justify="center")
    if dlist:
        output_text.insert(END, f"\n{'*' * 25} {diagnostic_name} {'*' * 25}\n", "center")
        
        table_data = get_table_data(diagnostic_name, dlist)
        
        if diagnostic_name == "TRACES":
            headers = ["Timestamp", "Scope", "Label"]
        elif diagnostic_name == "METRICS":
            headers = ["Timestamp", "Label", "Value"]
        elif diagnostic_name == "COUNTERS":
            headers = ["Timestamp", "Label", "Count"]
        
        formatted_data = [list(row.values()) for row in table_data]
        output_text.insert(END, tabulate(formatted_data, headers=headers, tablefmt="fancy_grid"), "center")
        output_text.insert(END, f"\n{'*' * 70}\n", "center")
    else:
        output_text.insert(END, f"No {diagnostic_name} data available.\n", "center")

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

# GUI Setup
root = Tk()
root.title("Hex Data Diagnostic Parser")
root.geometry("800x600")
root.configure(bg="#282C34")

title_label = Label(root, text="Hex Data Diagnostic Parser", font=("Arial", 18, "bold"), fg="#61AFEF", bg="#282C34")
title_label.pack(pady=10)

file_path_label = Label(root, text="Enter Binary File Path:", font=("Arial", 12), fg="white", bg="#282C34")
file_path_label.pack(pady=5)

file_path_entry = Entry(root, font=("Arial", 12), width=60)
file_path_entry.pack(pady=5)

convert_button = Button(root, text="Convert", command=convert_data, font=("Arial", 12), bg="#98C379", fg="white", width=15)
convert_button.pack(pady=10)

output_frame = Frame(root)
output_frame.pack(pady=10, fill=BOTH, expand=True)

scrollbar = Scrollbar(output_frame)
scrollbar.pack(side="right", fill="y")

output_text = Text(output_frame, wrap="word", yscrollcommand=scrollbar.set, font=("Courier New", 10), bg="#000000", fg="white", padx=10, pady=10)
output_text.tag_configure("center", justify="center")
output_text.pack(fill=BOTH, expand=True)

scrollbar.config(command=output_text.yview)

root.mainloop()
