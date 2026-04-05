import os
import re

def clean_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # Remove decorative headers `# ──...`
    content = re.sub(r'#\s*─+.*', '', content)
    
    # Remove file-level docstring which is unmistakably AI
    content = re.sub(r'^"""\s*NSE Alpha Engine.*?"""\n?', '', content, flags=re.DOTALL)
    
    # Clean multiple blank lines
    content = re.sub(r'\n{3,}', '\n\n', content)
    
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content.strip() + '\n')

for root, _, files in os.walk(r'c:\MAINDOMAIN\PRODUCTS\NSEAlphaFinder'):
    for file in files:
        if file.endswith('.py') and 'venv' not in root and 'build' not in root and file != 'cleaner.py':
            clean_file(os.path.join(root, file))
