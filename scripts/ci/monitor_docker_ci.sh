#!/usr/bin/env bash
# Monitor the latest Docker CI workflow run and report status when complete
# Usage: ./scripts/ci/monitor_docker_ci.sh [--poll-interval SECONDS]

set -euo pipefail

POLL_INTERVAL="${1:-30}"
WORKFLOW_NAME="ci.yml"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }

# Get the latest run ID for the Docker CI workflow
get_latest_run() {
    gh run list --workflow="$WORKFLOW_NAME" --limit=1 --json databaseId,status,conclusion,headBranch,createdAt,displayTitle 2>/dev/null
}

# Check if gh CLI is available
if ! command -v gh &> /dev/null; then
    log_error "GitHub CLI (gh) is not installed or not in PATH"
    exit 1
fi

# Check if authenticated
if ! gh auth status &> /dev/null; then
    log_error "Not authenticated with GitHub CLI. Run 'gh auth login'"
    exit 1
fi

log_info "Monitoring Docker CI workflow (${WORKFLOW_NAME})..."
log_info "Poll interval: ${POLL_INTERVAL}s"
echo ""

# Get initial run info
RUN_JSON=$(get_latest_run)
RUN_ID=$(echo "$RUN_JSON" | jq -r '.[0].databaseId')
BRANCH=$(echo "$RUN_JSON" | jq -r '.[0].headBranch')
TITLE=$(echo "$RUN_JSON" | jq -r '.[0].displayTitle')
CREATED=$(echo "$RUN_JSON" | jq -r '.[0].createdAt')

if [ -z "$RUN_ID" ] || [ "$RUN_ID" = "null" ]; then
    log_error "No runs found for workflow $WORKFLOW_NAME"
    exit 1
fi

log_info "Tracking run #${RUN_ID}"
log_info "Branch: ${BRANCH}"
log_info "Title: ${TITLE}"
log_info "Created: ${CREATED}"
echo ""

# Monitor loop
SPIN_CHARS='|/-\'
SPIN_IDX=0
while true; do
    RUN_JSON=$(get_latest_run)
    STATUS=$(echo "$RUN_JSON" | jq -r '.[0].status')
    CONCLUSION=$(echo "$RUN_JSON" | jq -r '.[0].conclusion')
    
    if [ "$STATUS" = "completed" ]; then
        echo ""
        echo "========================================"
        
        if [ "$CONCLUSION" = "success" ]; then
            log_success "Build completed successfully!"
            echo "========================================"
            gh run view "$RUN_ID" 2>/dev/null | head -30
            exit 0
        else
            log_error "Build failed with conclusion: $CONCLUSION"
            echo "========================================"
            echo ""
            
            # Get failed job logs
            log_info "Fetching failure details..."
            echo ""
            
            # Get annotations (error messages)
            echo "--- ANNOTATIONS ---"
            gh run view "$RUN_ID" 2>/dev/null | grep -A100 "ANNOTATIONS" | head -50
            echo ""
            
            # Try to get failed step logs
            echo "--- FAILED STEP LOG (last 80 lines) ---"
            gh run view "$RUN_ID" --log-failed 2>/dev/null | tail -80 || {
                log_warn "Could not fetch failed logs, trying full log..."
                gh run view "$RUN_ID" --log 2>/dev/null | tail -80
            }
            
            echo ""
            echo "--- RUN SUMMARY ---"
            gh run view "$RUN_ID" 2>/dev/null | head -40
            
            exit 1
        fi
    fi
    
    # Show spinner while waiting
    SPIN_CHAR="${SPIN_CHARS:SPIN_IDX:1}"
    SPIN_IDX=$(( (SPIN_IDX + 1) % 4 ))
    printf "\r${YELLOW}[%s]${NC} Status: %-12s (checking every %ds, Ctrl+C to stop)" "$SPIN_CHAR" "$STATUS" "$POLL_INTERVAL"
    
    sleep "$POLL_INTERVAL"
done

