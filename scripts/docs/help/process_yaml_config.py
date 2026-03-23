#!/usr/bin/env python3
"""
Enhanced documentation processor with YAML-based configuration
Supports content mapping, navigation structure, and automatic index generation
"""

import yaml
import os
import glob
import shutil
import sys
from pathlib import Path

def process_config(source_base_dir, target_dir, config_file):
    """Process YAML configuration and copy files according to mapping rules"""
    
    if not os.path.exists(config_file):
        print(f"Warning: Config {config_file} not found")
        return
    
    with open(config_file, 'r', encoding='utf-8') as f:
        config = yaml.safe_load(f)
    
    os.makedirs(target_dir, exist_ok=True)
    
    # Process navigation sections
    navigation = config.get('navigation', {})
    nav_links = []
    
    for section_key, section in navigation.items():
        section_dir = os.path.join(target_dir, section_key.lower())
        os.makedirs(section_dir, exist_ok=True)
        
        # Generate section index
        index_content = f"""---
title: {section['title']}
---

# {section['title']}

{section['description']}

"""
        
        items = section.get('items', [])
        
        # Special handling for CLI section - check if CLI docs already exist
        if section_key.lower() == 'cli' and os.path.exists(os.path.join(target_dir, 'cli', 'index.md')):
            # CLI docs exist, add them to the index
            index_content += "- [CLI Commands](index.md)\n"
        
        for item in items:
            source_pattern = item.get('source')
            target_path = item['target']
            
            # Handle items without source (like CLI commands.md)
            if not source_pattern:
                # Just add to index, file should be generated elsewhere
                title = item.get('title', os.path.splitext(os.path.basename(target_path))[0])
                if target_path.startswith(section_key.lower() + '/'):
                    link_path = os.path.relpath(os.path.join(target_dir, target_path), section_dir).replace('\\', '/')
                else:
                    link_path = target_path
                index_content += f"- [{title}]({link_path})\n"
                continue
            
            # Handle glob patterns
            if '**' in source_pattern or '*' in source_pattern:
                source_files = glob.glob(os.path.join(source_base_dir, source_pattern), recursive=True)
                for source_file in source_files:
                    if os.path.isfile(source_file):
                        rel_path = os.path.relpath(source_file, source_base_dir)
                        if item.get('preserve_structure'):
                            target_file = os.path.join(target_dir, target_path, os.path.relpath(source_file, os.path.join(source_base_dir, os.path.dirname(source_pattern.replace('**/*', '')))))
                        else:
                            filename = os.path.basename(source_file)
                            target_file = os.path.join(target_dir, target_path, filename)
                        
                        os.makedirs(os.path.dirname(target_file), exist_ok=True)
                        shutil.copy2(source_file, target_file)
                        print(f"  Copied: {rel_path} -> {os.path.relpath(target_file, target_dir)}")
                        
                        # Add to index
                        title = item.get('title', os.path.splitext(filename)[0])
                        link_path = os.path.relpath(target_file, section_dir).replace('\\', '/')
                        index_content += f"- [{title}]({link_path})\n"
            else:
                # Single file
                source_file = os.path.join(source_base_dir, source_pattern)
                if os.path.isfile(source_file):
                    target_file = os.path.join(target_dir, target_path)
                    os.makedirs(os.path.dirname(target_file), exist_ok=True)
                    shutil.copy2(source_file, target_file)
                    print(f"  Copied: {source_pattern} -> {target_path}")
                    
                    # Add to index
                    title = item.get('title', os.path.splitext(os.path.basename(source_file))[0])
                    link_path = os.path.relpath(target_file, section_dir).replace('\\', '/')
                    index_content += f"- [{title}]({link_path})\n"
        
        # Write section index (skip if index.md already exists)
        index_path = os.path.join(section_dir, 'index.md')
        if not os.path.exists(index_path):
            with open(index_path, 'w', encoding='utf-8') as f:
                f.write(index_content)
        
        nav_links.append((section_key.lower(), section['title'], section['description']))
    
    # Generate main navigation index
    site_config = config.get('site', {})
    main_index = f"""---
title: {site_config.get('title', 'Documentation')}
---

# {site_config.get('title', 'Documentation')}

{site_config.get('description', '')}

## Navigation

"""
    
    for link, title, desc in nav_links:
        main_index += f"- [{title}]({link}/) - {desc}\n"
    
    # Add link to demo overview as default content
    main_index += f"\n## Getting Started\n\n[View Demo Overview](demo/overview.md)\n"
    
    # Write main index
    index_path = os.path.join(target_dir, 'index.md')
    with open(index_path, 'w', encoding='utf-8') as f:
        f.write(main_index)
    
    print("YAML config processing completed")

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: process_yaml_config.py <source_base_dir> <target_dir> <config_file>")
        sys.exit(1)
    
    source_base_dir, target_dir, config_file = sys.argv[1:4]
    process_config(source_base_dir, target_dir, config_file)