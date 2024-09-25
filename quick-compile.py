import argparse
import os
import subprocess
import re
from pycparser import c_parser # pip install pycparser

# Constants
CLANG_PATH = 'C:/Users/Juani/LLVM/src/llvm/build/bootstrap/install/bin/clang-cl.exe'
CLANG_FLAGS = '/clang:-S /clang:-emit-llvm /clang:-O0'

OPT_PATH = 'C:/Users/Juani/LLVM/src/llvm/build/bootstrap/install/bin/opt'

DEFAULT_WINGSPAN_PATH = '../wingspan.dll'
DEFAULT_KEEP_UNOPTIMIZED_CODE=False

DESIRED_IR_ATTRSET = '{ nounwind uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }'

# Parse the args
def parse_args():
    parser = argparse.ArgumentParser(description="Quick compile suite for Wingspan Optimization Suite.")
    parser.add_argument("filename", help="Path to the .c file to compile.")
    parser.add_argument("-ws", dest="wingspan_path", help="Path to the .dll of the optimization suite", default=DEFAULT_WINGSPAN_PATH)
    parser.add_argument("-k", dest="keep_unoptimized_code", help="Keep the .ll code obtained before running the wingspan-mem2reg pass.", action="store_true")
    return parser.parse_args()

# Get a list of all functions declared in the .c
def get_implemented_function_names_in_c_code(filepath):
    # Leemos el archivo
    with open(filepath, 'r') as file:
        code = file.read()
    
    # Eliminamos las directivas al preprocesador
    code_without_directives = re.sub(r'^\s*#.*$', '', code, flags=re.MULTILINE)
    
    # Parseamos el código sin directivas
    parser = c_parser.CParser()
    ast = parser.parse(code_without_directives)
    
    # Extraemos los nombres de las funciones
    function_names = []
    def visit(node):
        # Comprobar si el nodo es una declaración de función
        if node.__class__.__name__ == 'FuncDef':
            function_names.append(node.decl.name)

        for child_name, child in node.children():
            visit(child)

    visit(ast)
    
    return function_names

# Compile into llvm ir
def compile(filename):
    base_dir = os.path.dirname(filename)
    base_name = os.path.splitext(os.path.basename(filename))[0]
    output_filename = os.path.join(base_dir, base_name + ".ll")
    
    command = f"{CLANG_PATH} {CLANG_FLAGS} {filename} -o {output_filename}"
    
    try:
        subprocess.run(command, check=True, shell=True)
    except subprocess.CalledProcessError as e:
        print(f"Error during compilation: {e}")

    return base_name + ".ll"

# Get a list of all functions declared in the .ll, as well as the number of the attribute sets.
def do_ir_analysis(filepath):
    # Abrimos el archivo y leemos todas sus líneas
    with open(filepath, 'r') as file:
        lines = file.readlines()

    # 1. Extraemos los nombres de las funciones
    function_names = []
    for line in lines:
        # Buscamos definiciones de funciones que comienzan con "define"
        match = re.match(r'^\s*define\s+.*@(\w+)\s*\(', line)
        if match:
            function_names.append(match.group(1))

    # 2. Encontramos el número máximo de attribute sets
    max_attribute_set = 0
    for line in lines:
        match = re.match(r'^\s*attributes\s+#(\d+)\s*=.*', line)
        if match:
            attribute_number = int(match.group(1))
            if attribute_number > max_attribute_set:
                max_attribute_set = attribute_number

    return function_names, max_attribute_set

# Create a new attribute set for user-implemented functions, and change de ir so they get assigned that attribute set.
def adjust_attribute_sets(ir, c_functions, ir_functions, new_attrset_id):
    # Leemos el contenido del archivo IR
    with open(ir, "r") as file:
        ir_content = file.readlines()
    
    # Definimos el nuevo conjunto de atributos que debe insertarse
    new_attrset = "attributes #" + str(new_attrset_id) + " = " + DESIRED_IR_ATTRSET + "\n"

    # Buscamos la última línea de conjunto de atributos y la posición de inserción
    last_attr_line_idx = -1
    for idx, line in enumerate(ir_content):
        if line.startswith("attributes #"):
            last_attr_line_idx = idx
    
    # Insertamos el nuevo conjunto de atributos inmediatamente después de la última definición encontrada
    ir_content.insert(last_attr_line_idx + 1, new_attrset)
    
    # Ahora recorremos las funciones implementadas en IR y cambiamos sus conjuntos de atributos si coinciden con las de C
    for idx, line in enumerate(ir_content):
        if line.startswith("define"):
            # Obtenemos el nombre de la función definida en IR
            func_name_start = line.find("@") + 1
            func_name_end = line.find("(", func_name_start)
            ir_function_name = line[func_name_start:func_name_end]
            
            # Si esta función también está implementada en el código fuente C, ajustamos el attribute set
            if ir_function_name in c_functions and ir_function_name in ir_functions:
                # Reemplazamos el conjunto de atributos por el nuevo
                current_attr_idx = line.rfind("#")
                if current_attr_idx != -1:
                    new_line = line[:current_attr_idx] + "#" + str(new_attrset_id) + " {\n"
                    ir_content[idx] = new_line
    
    # Guardamos los cambios en el archivo IR
    with open(ir, "w") as file:
        file.writelines(ir_content)

# Run the wingspan-mem2reg pass on the adjusted ir
def optimize(ir, opt_dll):
    ir_optimized = ir.replace(".ll", "-opt.ll")

    # When manually running opt, you're supposed to surround the names of the passes with ''. 
    # For example, command should read --passes='wingspan-mem2reg'. However, when running it
    # through python, for some weird reason, if you do that, it doesn't work.
    command = f"{OPT_PATH} --load-pass-plugin {opt_dll} --passes=wingspan-mem2reg -S -o {ir_optimized} {ir}"

    try:
        subprocess.run(command, check=True, shell=True)
    except subprocess.CalledProcessError as e:
        print(f"Error during optimization: {e}")

    return ir_optimized

def main():
    args = parse_args()
    
    compiled = compile(args.filename)
    print(f"[CLANG] Successfully compiled into LLVM IR at: {compiled}")
    
    c_functions = get_implemented_function_names_in_c_code(args.filename)
    ir_functions, max_attr_set_id = do_ir_analysis(compiled)
    new_attr_set_id = max_attr_set_id + 1
    
    adjust_attribute_sets(compiled, c_functions, ir_functions, new_attr_set_id)
    print(f"[QUICK-COMPILE] Successfully adjusted the attribute sets of {compiled}")

    optimized = optimize(compiled, args.wingspan_path)
    print(f"[QUICK-COMPILE] Successfully ran wingspan-mem2reg on {compiled}. Optimized code is at {optimized}")

    # TO-DO: Delete the original .ll file if -k wasn't set 

if __name__ == "__main__":
    main()