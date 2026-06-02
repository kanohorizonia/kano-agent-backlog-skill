# migrate-cache-v003.ps1 - Migrate cache structure for v0.0.3

Write-Host "🔄 Migrating cache structure to v0.0.3..." -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path ".cache")) {
    Write-Host "✅ No old cache structure found. Nothing to migrate." -ForegroundColor Green
    exit 0
}

Write-Host "📁 Creating .kano/cache/backlog/..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path ".kano\cache\backlog" | Out-Null

if (Test-Path ".cache\repo_chunks.sqlite3") {
    Write-Host "📦 Migrating chunks database..." -ForegroundColor Yellow
    Copy-Item ".cache\repo_chunks.sqlite3" ".kano\cache\backlog\chunks.repo.v1.db"
    $size = (Get-Item ".cache\repo_chunks.sqlite3").Length / 1MB
    Write-Host "  ✓ chunks.repo.v1.db ($([math]::Round($size, 1)) MB)" -ForegroundColor Green
}

if (Test-Path ".cache\repo_build_status.json") {
    Write-Host "📊 Migrating build status..." -ForegroundColor Yellow
    Copy-Item ".cache\repo_build_status.json" ".kano\cache\backlog\chunks.repo.v1.status"
    Write-Host "  ✓ chunks.repo.v1.status" -ForegroundColor Green
}

if (Test-Path ".cache\vectors") {
    Write-Host "🔢 Migrating vector databases..." -ForegroundColor Yellow
    Get-ChildItem ".cache\vectors\*.sqlite3" | ForEach-Object {
        $basename = $_.Name
        if ($basename -match "repo_chunks\.(.+)\.sqlite3") {
            $hash = $matches[1]
            $hashShort = $hash.Substring(0, 8)
            
            Copy-Item $_.FullName ".kano\cache\backlog\vectors.repo.noop-d1536.$hashShort.db"
            $size = $_.Length / 1MB
            Write-Host "  ✓ vectors.repo.noop-d1536.$hashShort.db ($([math]::Roe, 1)) MB)" -ForegroundColor Green
            
            $metaFile = $_.FullName -replace "\.sqlite3$", ".meta.json"
            if (Test-Path $metaFile) {
                Copy-Item $metaFile ".kano\cache\backlog\vectors.repo.noop-d1536.$hashShort.meta"
                Write-Host "  ✓ vectors.repo.noop-d1536.$hashShort.meta" -ForegroundColor Green
            }
        }
    }
}

Write-Host ""
Write-Host "📦 Migrating backlog corpus (product-specific caches)..." -ForegroundColor Yellow

if (Test-Path "_kano\backlog\products") {
    Get-ChildItem "_kano\backlog\products" -Directory | ForEach-Object {
        $productName = $_.Name
        $cacheDir = Join-Path $_.FullName ".cache"
        
        if (Test-Path $cacheDir) {
            Write-Host "  Processing product: $productName" -ForegroundColor Cyan
            
            $chunksFile = Join-Path $cacheDir "chunks.sqlite3"
            if (Test-Path $chunksFile) {
                Copy-Item $chunksFile ".kano\cache\backlog\chunks.backlog.$productName.v1.db"
                $size = (Get-Item $chunksFile).Length / 1MB
                Write-Host "    ✓ chunks.backlog.$productName.v1.db ($([math]::Round($size, 1)) MB)" -ForegroundColor Green
            }
            
            $vectorDir = Join-Path $cacheDir "vector"
            if (Test-Path $vectorDir) {
                Get-ChildItem "$vectorDir\*.sqlite3" | ForEach-Object {
                    $basename = $_.Name
                    if ($basename -match "backlog\.(.+)\.sqlite3") {
                        $hash = $matches[1]
                        $hashShort = $hash.Substring(0, 8)
                        
                        Copy-Item $_.FullName ".kano\cache\backlog\vectors.backlog.$productName.noop-d1536.$hashShort.db"
                        $size = $_.Length / 1MB
                        Write-Host "    ✓ vectors.backlog.$productName.noop-d1536.$hashShort.db ($([math]::Round($size, 1)) MB)" -ForegroundColor Green
                    }
                }
            }
        }
    }
}

Write-Host ""
Write-Host "🧹 Cleaning up obsolete directories in _kano/backlog/..." -ForegroundColor Yellow

function Safe-Remove {
    param($dir)
    if (Test-Path $dir) {
        $items = Get-ChildItem $dir -ErrorAction SilentlyContinue
        if ($items.Count -eq 0) {
            Write-Host "  🗑️  Removing empty directory: $dir" -ForegroundColor Gray
            Remove-Item $dir -Recurse -Force
        } else {
            Write-Host "  ⚠️  Directory not empty, skipping: $dir" -ForegroundColor Yellow
            Write-Host "     Please review contents manually." -ForegroundColor Gray
        }
    }
}

Safe-Remove "_kano\backlog\items"
Safe-Remove "_kano\backlog\views"
Safe-Remove "_kano\backlog\sandboxes"
Safe-Remove "_kano\backlog\_tmp_tests"

Write-Host ""
Write-Host "✅ Migration complete!" -ForegroundColor Green
Write-Host ""
Write-Host "📋 Summary:" -ForegroundColor Cyan
Write-Host "  Old location: .cache\" -ForegroundColor Gray
Write-Host "  New location: .kano\cache\backlog\" -ForegroundColor Gray
Write-Host ""
Write-Host "⚠️  Old files are preserved. To remove them:" -ForegroundColor Yellow
Write-Host "  Repo corpus: Remove-Item .cache\repo_chunks.sqlite3, .cache\repo_build_status.json, .cache\vectors\ -Recurse -Force" -ForegroundColor Gray
Write-Host "  Backlog corpus: Get-ChildItem _kano\backlog\products\*\.cache -Recurse | Remove-Item -Recurse -Force" -ForegroundColor Gray
Write-Host ""
