@Library([
    'kano-jenkins-common-pipeline-library@main',
    'kano-jenkins-agent-skill-pipeline-library@main'
]) _

agentSkillPipelineFromProjectConfig(
    configPath: '.jenkins/config/agent-skill-pipeline.json',
    config: [
        bDryRun: params.DRY_RUN,
        bRunBuildPhase: false,
        bRunReleasePhase: params.PUBLISH_RELEASE,
        releaseTag: params.RELEASE_TAG,
        releaseTitle: params.RELEASE_TAG
    ]
)
