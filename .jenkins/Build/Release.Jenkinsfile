@Library(['kano-jenkins-common-pipeline-library@main', 'kano-jenkins-agent-skill-pipeline-library@main']) _

pipeline {
    agent none

    parameters {
        booleanParam(name: 'DRY_RUN', defaultValue: true, description: 'Render the Build/Release plan and stop before checkout/build/export.')
    }

    stages {
        stage('Run Build/Release') {
            steps {
                script {
                    agentSkillPipelineFromProjectConfig(
                        configPath: '.jenkins/config/agent-skill-pipeline.json',
                        bootstrapAgentLabel: 'windows && lightweight',
                        config: [
                            bDryRun                    : params.DRY_RUN,
                            bRunBuildPhase             : !params.DRY_RUN,
                            bRunReleasePhase           : false,
                            bPublishSite               : false,
                            bCreateGitHubRelease       : false,
                            bRequireExplicitReleaseTag : false,
                            provisionalBuildDisplayName: "#${env.BUILD_NUMBER ?: '0'} Build_Shipping queued",
                            latestQueuedBuildLockName  : 'kano-agent-backlog-skill-build-release',
                        ]
                    )
                }
            }
        }
    }
}
