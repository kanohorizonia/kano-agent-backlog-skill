#!/bin/bash
# migrate-cache-v003.sh - Migrate cache structure for v0.0.3

set -e

echo "🔄 Migrating cache structure to v0.0.3..."
echo ""

if [ ! -d ".cache" ]; then
    echo "✅ No old cache structure found. Nothing to migrate."
    exit 0
fi

echo "📁 Creating .kano/cache/backlog/..."
mkdir -p .kano/cache/backlog

if [ -f ".cache/repo_chunks.sqlite3" ]; then
    echo "📦 Migrating chunks database..."
    cp .cache/repo_chunks.sqlite3 .kano/cache/backlog/chunks.repo.v1.db
    SIZE=$(du -h .cache/repo_chunks.sqlite3 | cut -f1)
    echo "  ✓ chunks.repo.v1.db ($SIZE)"
fi

if [ -f ".cache/repo_build_status.json" ]; then
    echo "📊 Migrating build status..."
    cp .cache/repo_build_status.json .kano/cache/backlog/chunks.repo.v1.status
    echo "  ✓ chunks.repo.v1.status"
fi

if [ -d ".cache/vectors" ]; then
    echo "🔢 Migrating vector databases..."
    for db in .cache/vectors/*.sqlite3; do
        if [ -f "$db" ]; then
            basename=$(basename "$db")
            hash=$(echo "$basename" | sed 's/repo_chunks\.\(.*\)\.sqlite3/\1/')
            hash_short=${hash:0:8}
            
            cp "$db" ".kano/cache/backlog/vectors.repo.noop-d1536.${hash_short}.db"
            SIZE=$(du -h "$db" | cut -f1)
            echo "  ✓ vectors.repo.noop-d1536.${hash_short}.db ($SIZE)"
            
            meta="${db%.sqlite3}.meta.json"
            if [ -f "$meta" ]; then
                cp "$meta" ".kano/cache/backlog/vectors.repo.noop-d1536.${hash_short}.meta"
                echo "  ✓ vectors.repo.noop-d1536.${hash_short}.meta"
            fi
        fi
    done
fi

echo ""
echo "📦 Migrating backlog corpus (product-specific caches)..."

if [ -d "_kano/backlog/products" ]; then
    for product_dir in _kano/backlog/products/*/; do
        if [ -d "$product_dir" ]; then
            product_name=$(basename "$product_dir")
            cache_dir="${product_dir}.cache"
            
            if [ -d "$cache_dir" ]; then
                echo "  Processing product: $product_name"
                
                if [ -f "$cache_dir/chunks.sqlite3" ]; then
                    cp "$cache_dir/chunks.sqlite3" ".kano/cache/backlog/chunks.backlog.${product_name}.v1.db"
                    SIZE=$(du -h "$cache_dir/chunks.sqlite3" | cut -f1)
                    echo "    ✓ chunks.backlog.${product_name}.v1.db ($SIZE)"
                fi
                
                if [ -d "$cache_dir/vector" ]; then
                    for db in "$cache_dir/vector"/*.sqlite3; do
                        if [ -f "$db" ]; then
                            basename=$(basename "$db")
                            hash=$(echo "$basename" | sed 's/backlog\.\(.*\)\.sqlite3/\1/')
                            hash_short=${hash:0:8}
                            
                            cp "$db" ".kano/cache/backlog/vectors.backlog.${product_name}.noop-d1536.${hash_short}.db"
                            SIZE=$(du -h "$db" | cut -f1)
                            echo "    ✓ vectors.backlog.${product_name}.noop-d1536.${hash_short}.db ($SIZE)"
                        fi
                    done
                fi
            fi
        fi
    done
fi

echo ""
echo "🧹 Cleaning up obsolete directories in _kano/backlog/..."

safe_remove() {
    local dir=$1
    if [ -d "$dir" ]; then
        if [ -z "$(ls -A "$dir" 2>/dev/null)" ]; then
            echo "  🗑️  Removing empty directory: $dir"
            rm -rf "$dir"
        else
            echo "  ⚠️  Directory not empty, skipping: $dir"
            echo "     Please review contents manually."
        fi
    fi
}

safe_remove "_kano/backlog/items"
safe_remove "_kano/backlog/views"
safe_remove "_kano/backlog/sandboxes"
safe_remove "_kano/backlog/_tmp_tests"

echo ""
echo "✅ Migration complete!"
echo ""
echo "📋 Summary:"
echo "  Old location: .cache/"
echo "  New location: .kano/cache/backlog/"
echo ""
echo "⚠️  Old files are preserved. To remove them:"
echo "  Repo corpus: rm -rf .cache/repo_chunks.sqlite3 .cache/repo_build_status.json .cache/vectors/"
echo "  Backlog corpus: rm -rf _kano/backlog/products/*/.cache/"
echo ""
