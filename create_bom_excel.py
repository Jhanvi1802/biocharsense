"""Generate BiocharSense procurement Excel sheet."""
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment, Border, Side
from openpyxl.utils import get_column_letter

# (item, vendor, link, cost_per_unit, qty_suggested, availability, notes)
ITEMS = [
    # Items already in Robu cart
    ("Samsung INR18650-30Q 3000mAh Li-Ion Battery",
     "Robu", "https://robu.in/product/samsung-18650-30q-li-ion-battery/",
     578, 2, "In stock - in cart",
     "Genuine cell (lab-verified by Robu). Need more if scaling deployment."),

    ("Rubycon 1000uF 25V Electrolytic Capacitor",
     "Robu", "https://robu.in/product/1000uf-25v-electrolytic-capacitor-dip-pack-of-5/",
     15, 2, "In stock - in cart",
     "Bulk decoupling cap for power line. Quantity is pack-of-5 -> 2 packs = 10 caps."),

    ("3A 18650 Lithium Battery Charger Module (Type-C)",
     "Robu", "https://robu.in/product/3a-18650-lithium-battery-charger-module-with-battery-box-5v-type-c/",
     293, 3, "In stock - in cart",
     "Battery holder + boost converter + charging in one. Senior-approved."),

    # Items to order from Amazon
    ("ESP32 NodeMCU 38-pin DevKit V1 (Type-C)",
     "Amazon", "https://www.amazon.in/NodeMCU-Development-WROOM32-CP2102-Bluetooth/dp/B0DYK8S5WN",
     500, 1, "In stock on Amazon",
     "Main microcontroller. Approx price - check link for current."),

    ("MAX31855 K-Type Thermocouple Module (with thermocouple wire)",
     "Amazon", "https://www.amazon.in/K-Type-Thermocouple-Breakout-MAX31855-Included/dp/B08FZZTNBV",
     450, 1, "In stock on Amazon",
     "Includes basic K-type wire - good for bench testing while waiting for industrial probe."),

    ("Tactile Push Button Switch 6x6mm (pack of 10)",
     "Amazon", "https://www.amazon.in/6x6x6mm-Momentary-Push-Button-Switch/dp/B01GN79QF8",
     150, 1, "In stock on Amazon",
     "For setup-portal trigger on GPIO 4."),

    ("100nF (0.1uF) Ceramic Capacitors (BOJACK pack of 50)",
     "Amazon", "https://www.amazon.in/BOJACK-Capacitors-Low-Voltage-Dielectric-Capacitor/dp/B07X5XTDPB",
     200, 1, "In stock on Amazon",
     "High-frequency decoupling for MAX31855."),

    ("PG9 Cable Glands IP68 (pack of 10, ERH India)",
     "Amazon", "https://www.amazon.in/ERH-India-10-Cable-Gland/dp/B09TJNKZYF",
     200, 1, "In stock on Amazon",
     "For sealing cable entries in the enclosure."),

    ("Hammond Polycarbonate Enclosure 1554VB2GY (160x120x60)",
     "Amazon", "https://www.amazon.in/Hammond-PCB-Box-Enclosure-Grey/dp/B06ZYTJDQV",
     600, 1, "In stock on Amazon",
     "IP66 weatherproof. Verify listing says 'polycarbonate' not 'ABS'."),

    ("22 AWG Silicone Hookup Wire (multi-color, 200C rated)",
     "Amazon", "https://www.amazon.in/MECCANIXITY-Silicone-Electrical-Stranded-Appliances/dp/B0CG2SJXH3",
     400, 1, "In stock on Amazon",
     "Heat-resistant wire for inside enclosure."),

    ("Perfboard / General-Purpose PCB Double-sided 6x4cm (pack of 2)",
     "Amazon", "https://www.amazon.in/EZPHYSIKX-General-purpose-prototyping-Perforated/dp/B0CJGQLWGK",
     150, 1, "In stock on Amazon",
     "For clean soldering of all components."),

    # Tempsens - call/email, not e-commerce
    ("Industrial K-Type MI Thermocouple Probe (Inconel 600, 6mm dia, 500mm)",
     "Tempsens", "https://www.tempsens.com",
     2000, 1, "Quote pending - contact vendor",
     "Industrial-grade probe for biochar kiln. Ungrounded junction. Call/email."),

    ("Inconel 600 Thermowell (matching probe spec)",
     "Tempsens", "https://www.tempsens.com",
     1200, 1, "Quote pending - contact vendor",
     "Protects probe from tar/acid gases. Extends life from months to years."),

    ("K-Type Compensation Extension Cable (10m, fiberglass)",
     "Tempsens", "https://www.tempsens.com",
     600, 1, "Quote pending - contact vendor",
     "Bundle with probe order. Routes signal from kiln area to electronics box."),
]

wb = Workbook()
ws = wb.active
ws.title = "BiocharSense BOM"

# Headers
headers = ["#", "Item", "Vendor", "Purchase Link", "Cost/Unit (₹)", "Qty", "Total (₹)", "Availability", "Notes"]
ws.append(headers)

header_fill = PatternFill(start_color="1F4E78", end_color="1F4E78", fill_type="solid")
header_font = Font(name="Calibri", size=11, bold=True, color="FFFFFF")
header_align = Alignment(horizontal="center", vertical="center", wrap_text=True)
thin = Side(border_style="thin", color="999999")
border = Border(left=thin, right=thin, top=thin, bottom=thin)

for col_idx, _ in enumerate(headers, start=1):
    cell = ws.cell(row=1, column=col_idx)
    cell.fill = header_fill
    cell.font = header_font
    cell.alignment = header_align
    cell.border = border

# Status colors
status_fills = {
    "In stock - in cart":       PatternFill(start_color="C6EFCE", end_color="C6EFCE", fill_type="solid"),
    "In stock on Amazon":       PatternFill(start_color="FFEB9C", end_color="FFEB9C", fill_type="solid"),
    "Quote pending - contact vendor": PatternFill(start_color="FFC7CE", end_color="FFC7CE", fill_type="solid"),
}

# Data rows
for i, (item, vendor, link, cost, qty, availability, notes) in enumerate(ITEMS, start=1):
    row = i + 1
    ws.cell(row=row, column=1, value=i).alignment = Alignment(horizontal="center")
    ws.cell(row=row, column=2, value=item).alignment = Alignment(wrap_text=True, vertical="top")
    ws.cell(row=row, column=3, value=vendor).alignment = Alignment(horizontal="center", vertical="top")

    link_cell = ws.cell(row=row, column=4, value="Open link")
    link_cell.hyperlink = link
    link_cell.font = Font(color="0000FF", underline="single")
    link_cell.alignment = Alignment(horizontal="center", vertical="top")

    ws.cell(row=row, column=5, value=cost).number_format = "#,##0"
    ws.cell(row=row, column=5).alignment = Alignment(horizontal="right", vertical="top")
    ws.cell(row=row, column=6, value=qty).alignment = Alignment(horizontal="center", vertical="top")

    total_cell = ws.cell(row=row, column=7, value=f"=E{row}*F{row}")
    total_cell.number_format = "#,##0"
    total_cell.alignment = Alignment(horizontal="right", vertical="top")

    status_cell = ws.cell(row=row, column=8, value=availability)
    status_cell.alignment = Alignment(wrap_text=True, vertical="top", horizontal="center")
    if availability in status_fills:
        status_cell.fill = status_fills[availability]

    ws.cell(row=row, column=9, value=notes).alignment = Alignment(wrap_text=True, vertical="top")

    for c in range(1, 10):
        ws.cell(row=row, column=c).border = border

# Grand total row
total_row = len(ITEMS) + 2
ws.cell(row=total_row, column=6, value="Subtotal").font = Font(bold=True)
ws.cell(row=total_row, column=6).alignment = Alignment(horizontal="right")
gt = ws.cell(row=total_row, column=7, value=f"=SUM(G2:G{total_row-1})")
gt.font = Font(bold=True)
gt.number_format = "#,##0"
gt.fill = PatternFill(start_color="DDEBF7", end_color="DDEBF7", fill_type="solid")
gt.alignment = Alignment(horizontal="right")
gt.border = border

# Buffer note
note_row = total_row + 2
ws.cell(row=note_row, column=2, value="Note: Prices are approximate. Robu cart amounts are confirmed; Amazon items priced from listing estimates; Tempsens items pending quotation.").font = Font(italic=True, color="555555")
ws.merge_cells(start_row=note_row, start_column=2, end_row=note_row, end_column=9)

note_row2 = note_row + 1
ws.cell(row=note_row2, column=2, value="Add ~15-20% buffer for shipping, taxes (GST), and contingency before final approval.").font = Font(italic=True, color="555555")
ws.merge_cells(start_row=note_row2, start_column=2, end_row=note_row2, end_column=9)

# Column widths
widths = {1: 5, 2: 50, 3: 12, 4: 14, 5: 14, 6: 7, 7: 14, 8: 22, 9: 50}
for col, w in widths.items():
    ws.column_dimensions[get_column_letter(col)].width = w

# Row heights for data rows
for r in range(2, total_row):
    ws.row_dimensions[r].height = 50

ws.freeze_panes = "A2"

# Save
import os
out = "BiocharSense_BOM.xlsx"
wb.save(out)
print(f"Wrote {out} ({os.path.getsize(out):,} bytes, {len(ITEMS)} items)")
