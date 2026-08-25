[CmdletBinding()]
param(
	[switch]$Fetch,
	[string]$OutputRoot = "out/upstream-sync",
	[ValidateRange(1, 200)]
	[int]$CommitLimit = 30
)

$ErrorActionPreference = "Stop"

function Invoke-Git {
	param(
		[Parameter(Mandatory = $true)]
		[string[]]$Arguments,
		[switch]$AllowFailure
	)

	$previousErrorAction = $ErrorActionPreference
	$ErrorActionPreference = "Continue"
	try {
		$output = @(& git @Arguments 2>&1 | ForEach-Object { $_.ToString() })
		$exitCode = $LASTEXITCODE
	} finally {
		$ErrorActionPreference = $previousErrorAction
	}
	if (($exitCode -ne 0) -and !$AllowFailure) {
		throw "git $($Arguments -join ' ') failed with exit code $exitCode`n$($output -join "`n")"
	}
	return [pscustomobject]@{
		ExitCode = $exitCode
		Output = $output
	}
}

function Add-CodeBlock {
	param(
		[AllowNull()]
		[AllowEmptyCollection()]
		[AllowEmptyString()]
		[string[]]$Content
	)

	$script:reportLines.Add('```text')
	if ($Content.Count -eq 0) {
		$script:reportLines.Add('(none)')
	} else {
		foreach ($line in $Content) {
			$script:reportLines.Add($line)
		}
	}
	$script:reportLines.Add('```')
}

$rootResult = Invoke-Git -Arguments @('rev-parse', '--show-toplevel')
$repoRoot = $rootResult.Output[0].Trim()
Set-Location -LiteralPath $repoRoot

$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'out'))
$reportBase = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputRoot))
$comparison = [System.StringComparison]::OrdinalIgnoreCase
if (!$reportBase.StartsWith($allowedRoot + [System.IO.Path]::DirectorySeparatorChar, $comparison)) {
	throw "OutputRoot must resolve below the repository out directory."
}

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$reportDirectory = Join-Path $reportBase $timestamp
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$reportPath = Join-Path $reportDirectory 'audit.md'

$remoteRoles = [ordered]@{
	upstream = 'optional supplemental source'
	ayugram = 'public AyuGram source'
	telegram = 'official Telegram Desktop source'
	origin = "distribution fork publishing remote"
}

$remoteNames = (Invoke-Git -Arguments @('remote')).Output
$fetchLog = [System.Collections.Generic.List[string]]::new()
$fetchFailureCount = 0
if ($Fetch) {
	foreach ($remote in $remoteRoles.Keys) {
		if ($remoteNames -contains $remote) {
			$result = Invoke-Git -Arguments @('fetch', $remote, '--prune') -AllowFailure
			$fetchLog.Add("[$remote] exit=$($result.ExitCode)")
			if ($result.ExitCode -ne 0) {
				$fetchFailureCount++
			}
			foreach ($line in $result.Output) {
				$fetchLog.Add($line)
			}
		}
	}
}

$script:reportLines = [System.Collections.Generic.List[string]]::new()
$lines = $script:reportLines
$null = & {
$lines.Add('# AyuGram Upstream Audit')
$lines.Add('')
$lines.Add("- Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')")
$lines.Add("- Repository: $repoRoot")
$lines.Add("- HEAD: $((Invoke-Git -Arguments @('rev-parse', 'HEAD')).Output[0])")
$lines.Add("- Branch: $((Invoke-Git -Arguments @('branch', '--show-current')).Output[0])")
$lines.Add("- Fetch requested: $Fetch")
$lines.Add('')

$lines.Add('## Workspace')
$lines.Add('')
$lines.Add('### Status')
Add-CodeBlock -Content (Invoke-Git -Arguments @('status', '--short', '--branch')).Output
$lines.Add('')
$lines.Add('### Stashes')
Add-CodeBlock -Content (Invoke-Git -Arguments @('stash', 'list')).Output
$lines.Add('')
$lines.Add('### Diff check')
$diffCheck = Invoke-Git -Arguments @('diff', '--check') -AllowFailure
$lines.Add("Exit code: $($diffCheck.ExitCode)")
Add-CodeBlock -Content $diffCheck.Output
$lines.Add('')

$rootArtifacts = @(Get-ChildItem -LiteralPath $repoRoot -Directory -Force | Where-Object {
	$_.Name -eq 'build' -or $_.Name -like 'cmake-build-*'
} | ForEach-Object { $_.FullName })
$lines.Add('### Generated root directories')
Add-CodeBlock -Content $rootArtifacts
$lines.Add('')

$lines.Add('## Remotes')
$lines.Add('')
$lines.Add('| Remote | Role | URL |')
$lines.Add('| --- | --- | --- |')
foreach ($remote in $remoteRoles.Keys) {
	if ($remoteNames -contains $remote) {
		$url = (Invoke-Git -Arguments @('remote', 'get-url', $remote)).Output[0]
		$lines.Add("| $remote | $($remoteRoles[$remote]) | $url |")
	} else {
		$lines.Add("| $remote | $($remoteRoles[$remote]) | missing |")
	}
}
$lines.Add('')

if ($Fetch) {
	$lines.Add('### Fetch log')
	$lines.Add("Failures: $fetchFailureCount")
	Add-CodeBlock -Content $fetchLog.ToArray()
	$lines.Add('')
}

foreach ($remote in $remoteRoles.Keys) {
	if (!($remoteNames -contains $remote)) {
		continue
	}
	$lines.Add("## $remote branches")
	$lines.Add('')
	$lines.Add('| Ref | Ahead of HEAD | Behind HEAD | Commit | Date | Subject |')
	$lines.Add('| --- | ---: | ---: | --- | --- | --- |')
	$refs = (Invoke-Git -Arguments @(
		'for-each-ref',
		'--sort=-committerdate',
		'--format=%(refname:short)|%(objectname)|%(committerdate:iso8601-strict)|%(subject)',
		"refs/remotes/$remote"
	)).Output
	$interesting = [System.Collections.Generic.List[string]]::new()
	foreach ($entry in $refs) {
		$parts = $entry -split '\|', 4
		if (($parts.Count -lt 4) -or $parts[0].EndsWith('/HEAD')) {
			continue
		}
		$ref = $parts[0]
		if (($ref -eq $remote) -or (($remote -eq 'telegram') -and ($ref -notin @('telegram/dev', 'telegram/master')))) {
			continue
		}
		$counts = (Invoke-Git -Arguments @('rev-list', '--left-right', '--count', "HEAD...$ref")).Output[0] -split '\s+'
		$behindHead = [int]$counts[0]
		$aheadOfHead = [int]$counts[1]
		$subject = $parts[3].Replace('|', '\|')
		$lines.Add("| $ref | $aheadOfHead | $behindHead | $($parts[1].Substring(0, 12)) | $($parts[2]) | $subject |")
		if ($aheadOfHead -gt 0) {
			$interesting.Add($ref)
		}
	}
	$lines.Add('')
	foreach ($ref in $interesting) {
		$lines.Add("### Unique commits: $ref")
		$commits = (Invoke-Git -Arguments @(
			'log',
			'--no-merges',
			"-$CommitLimit",
			'--date=short',
			'--pretty=format:%h %ad %s',
			"HEAD..$ref"
		)).Output
		Add-CodeBlock -Content $commits
		$lines.Add('')
	}
}

if (($remoteNames -contains 'telegram') -and ((Invoke-Git -Arguments @('show-ref', '--verify', '--quiet', 'refs/remotes/telegram/dev') -AllowFailure).ExitCode -eq 0)) {
	$lines.Add('## Latest Telegram release tags')
	$lines.Add('')
	$tags = (Invoke-Git -Arguments @('tag', '--sort=-version:refname', '--merged', 'telegram/dev', '--list', 'v[0-9]*')).Output | Select-Object -First 20
	Add-CodeBlock -Content @($tags)
	$lines.Add('')
}

$lines.Add('## Submodules')
$lines.Add('')
Add-CodeBlock -Content (Invoke-Git -Arguments @('submodule', 'status', '--recursive') -AllowFailure).Output
$lines.Add('')
$lines.Add('## Next action')
$lines.Add('')
$lines.Add('Inspect the reported unique commits and file diffs, classify selected and deferred changes, and prepare checkpoints before mutating the integration branch.')
$lines.Add('')

$content = $lines -join "`r`n"
[System.IO.File]::WriteAllText(
	$reportPath,
	$content + "`r`n",
	[System.Text.UTF8Encoding]::new($false))
}

Write-Output $reportPath
if ($fetchFailureCount -ne 0) {
	[Console]::Error.WriteLine("One or more remotes failed to fetch. Do not integrate from this audit.")
	exit 2
}
