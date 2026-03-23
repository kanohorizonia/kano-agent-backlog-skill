#!/usr/bin/env python3
"""
MkDocs Content Generator
Generates dynamic content files and navigation for MkDocs from YAML config
"""

import yaml
import os
import sys

def main():
    if len(sys.argv) != 4:
        print("Usage: mkdocs-content-generator.py <api_config_path> <mkdocs_config_path> <docs_dir>")
        sys.exit(1)
    
    api_config_path = sys.argv[1]
    mkdocs_config_path = sys.argv[2]
    docs_dir = sys.argv[3]
    
    with open(api_config_path, 'r') as f:
        config = yaml.safe_load(f)
    
    # Load existing MkDocs config
    with open(mkdocs_config_path, 'r') as f:
        mkdocs_config = yaml.safe_load(f)
    
    api_nav = config.get('api_navigation', {})
    
    # Add dynamic navigation to MkDocs config
    nav = []
    for nav_item in api_nav.get('nav', []):
        if 'items' in nav_item:
            nav_section = {nav_item['title']: []}
            for item in nav_item['items']:
                nav_section[nav_item['title']].append({item['title']: item['file']})
            nav.append(nav_section)
        else:
            nav.append({nav_item['title']: nav_item['file']})
    
    mkdocs_config['nav'] = nav
    
    # Write updated MkDocs config
    with open(mkdocs_config_path, 'w') as f:
        yaml.dump(mkdocs_config, f, default_flow_style=False, sort_keys=False)
    
    # Generate home page content
    with open(os.path.join(docs_dir, 'index.md'), 'w') as f:
        f.write(api_nav.get('home_content', '# API Documentation'))
    
    # Generate module documentation
    os.makedirs(os.path.join(docs_dir, 'api'), exist_ok=True)
    
    for nav_item in api_nav.get('nav', []):
        if 'items' in nav_item:
            for item in nav_item['items']:
                if 'module' in item:
                    module_name = item['module']
                    file_path = item['file']
                    output_path = os.path.join(docs_dir, file_path)
                    
                    # Ensure directory exists
                    os.makedirs(os.path.dirname(output_path), exist_ok=True)
                    
                    print(f"Generating {file_path} for {module_name}")
                    
                    # Generate module documentation with actual source scanning
                    with open(output_path, 'w') as f:
                        f.write(f"# {module_name} Module\n\n")
                        f.write(f"Python module: `{module_name}`\n\n")
                        
                        # Try to find the actual module directory
                        # Navigate from docs_dir to skill src directory
                        # docs_dir is like: /path/to/_ws/build/content_mkdocs/docs
                        # We need: /path/to/_ws/src/skill/src/module_name
                        build_dir = os.path.dirname(docs_dir)  # content_mkdocs
                        ws_dir = os.path.dirname(os.path.dirname(build_dir))  # _ws
                        skill_src_dir = os.path.join(ws_dir, 'src', 'skill', 'src')
                        module_path = os.path.join(skill_src_dir, module_name)
                        
                        if os.path.exists(module_path):
                            f.write("## Overview\n\n")
                            f.write(f"This module provides core functionality for {module_name}.\n\n")
                            f.write("## Classes and Functions\n\n")
                            
                            # Scan Python files in the module
                            for root, dirs, files in os.walk(module_path):
                                for file in files:
                                    if file.endswith('.py') and not file.startswith('__'):
                                        file_path = os.path.join(root, file)
                                        rel_path = os.path.relpath(file_path, module_path)
                                        module_file = rel_path.replace(os.sep, '.').replace('.py', '')
                                        
                                        f.write(f"### {module_file}\n\n")
                                        
                                        try:
                                            with open(file_path, 'r', encoding='utf-8') as src_file:
                                                content = src_file.read()
                                                
                                                # Extract classes
                                                import re
                                                classes = re.findall(r'^class\s+(\w+)', content, re.MULTILINE)
                                                functions = re.findall(r'^def\s+(\w+)', content, re.MULTILINE)
                                                
                                                if classes:
                                                    f.write("**Classes:**\n\n")
                                                    for cls in classes:
                                                        f.write(f"- `{cls}` - Class definition\n")
                                                    f.write("\n")
                                                
                                                if functions:
                                                    f.write("**Functions:**\n\n")
                                                    for func in functions:
                                                        if not func.startswith('_'):
                                                            f.write(f"- `{func}()` - Function\n")
                                                    f.write("\n")
                                                    
                                        except Exception as e:
                                            f.write(f"Error reading {file}: {e}\n\n")
                        else:
                            f.write("## Overview\n\n")
                            f.write(f"Module source not found at: {module_path}\n\n")
                            f.write("## Classes and Functions\n\n")
                            f.write("Documentation will be generated from source code.\n")
    
    print("MkDocs content and navigation generated from YAML")

if __name__ == '__main__':
    main()