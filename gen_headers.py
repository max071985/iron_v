import xml.etree.ElementTree as ET
import os

def generate_headers(svd_path, output_dir):
    tree = ET.parse(svd_path)
    root = tree.getroot()

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    peripherals = root.findall('.//peripheral')
    peri_dict = {p.find('name').text: p for p in peripherals}
    
    for peripheral in peripherals:
        name = peripheral.find('name').text
        base_address = peripheral.find('baseAddress').text
        description = peripheral.find('description')
        description_text = description.text if description is not None else ""
        
        derived_from_name = peripheral.get('derivedFrom')
        source_peripheral = peripheral
        if derived_from_name:
            source_peripheral = peri_dict.get(derived_from_name)
            if source_peripheral is None:
                print(f"Warning: {name} derived from unknown {derived_from_name}")
                source_peripheral = peripheral

        header_path = os.path.join(output_dir, f"{name.lower()}.h")
        with open(header_path, 'w') as f:
            f.write(f"/*\n * {name}.h\n * {description_text}\n * Base Address: {base_address}\n */\n\n")
            f.write(f"#ifndef {name}_H\n#define {name}_H\n\n")
            f.write(f"#include <stdint.h>\n\n")
            f.write(f"#define {name}_BASE {base_address}\n\n")
            
            registers = source_peripheral.findall('.//register')
            for register in registers:
                reg_name = register.find('name').text
                # Remove [%s] and %s
                reg_name = reg_name.replace('[%s]', '').replace('%s', '')
                offset = register.find('addressOffset').text
                reg_description = register.find('description')
                reg_desc_text = reg_description.text if reg_description is not None else ""
                
                f.write(f"// {reg_desc_text}\n")
                f.write(f"#define {name}_{reg_name}_REG ((volatile uint32_t *)({name}_BASE + {offset}))\n")
                
                fields = register.findall('.//field')
                for field in fields:
                    field_name = field.find('name').text
                    field_name = field_name.replace('[%s]', '').replace('%s', '')
                    bit_offset = int(field.find('bitOffset').text)
                    bit_width = int(field.find('bitWidth').text)
                    mask = ((1 << bit_width) - 1) << bit_offset
                    
                    f.write(f"#define {name}_{reg_name}_{field_name}_M (0x{mask:08X}U)\n")
                    f.write(f"#define {name}_{reg_name}_{field_name}_S ({bit_offset})\n")
                    f.write(f"#define {name}_{reg_name}_{field_name}_V(v) (((v) << {bit_offset}) & 0x{mask:08X}U)\n")
                f.write("\n")
            
            f.write(f"#endif // {name}_H\n")

if __name__ == "__main__":
    generate_headers('docs/knowledge/esp32c6.svd.txt', 'src/regs')
