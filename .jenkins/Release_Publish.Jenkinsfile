@Library([
    'kano-jenkins-common-pipeline-library@main',
    'kano-jenkins-agent-skill-pipeline-library@main'
]) _

agentSkillPipelineFromProjectConfig(
    configPath: '.jenkins/config/agent-skill-pipeline.json',
    config: [
        bDryRun: params.DRY_RUN,
        bRunBuildPhase: false,
        bRunReleasePhase: true
    ]
)
