@Library(['kano-jenkins-common-pipeline-library@main', 'kano-jenkins-agent-skill-pipeline-library@main']) _

pipeline {
    agent none

    parameters {
        booleanParam(name: 'DRY_RUN', defaultValue: true, description: 'Render the Release/Publish plan only by default.')
        booleanParam(name: 'PUBLISH_RELEASE', defaultValue: false, description: 'Enable Release/Publish side effects such as docs publish and GitHub release creation.')
        string(name: 'RELEASE_TAG', defaultValue: '', description: 'Required for non-dry-run publishing, for example v0.0.3.')
        string(name: 'UPSTREAM_BUILD_JOB', defaultValue: '', description: 'Upstream Build job name for artifact copy. If empty, use config default.')
        string(name: 'UPSTREAM_BUILD_NUMBER', defaultValue: '', description: 'Upstream build number for artifact copy. If empty, use latest successful upstream build.')
    }

    stages {
        stage('Run Release/Publish') {
            steps {
                script {
                    def upstreamConfig = [:]
                    if (params.UPSTREAM_BUILD_JOB?.trim()) {
                        upstreamConfig.upstreamBuildJob = params.UPSTREAM_BUILD_JOB.trim()
                    }
                    if (params.UPSTREAM_BUILD_NUMBER?.trim()) {
                        upstreamConfig.upstreamBuildNumber = params.UPSTREAM_BUILD_NUMBER.trim()
                    }
                    agentSkillPipelineFromProjectConfig(
                        configPath: '.jenkins/config/agent-skill-pipeline.json',
                        bootstrapAgentLabel: 'lightweight',
                        config: agentSkillParameterProfile(
                            params: params,
                            githubRepository: 'kanohorizonia/kano-agent-backlog-skill',
                            githubTokenCredentialId: 'github-release-token',
                            siteBuildCommand: 'bash src/shell/docs/build-and-deploy.sh --ci',
                            sitePublishCommand: 'bash src/shell/docs/build-and-deploy.sh --prep-deploy --push',
                            releaseApprovalMessage: 'Approve kano-agent-backlog-skill Release/Publish? Confirm tag, artifacts, docs, and release plan before proceeding.',
                        ) + [
                            bRunBuildPhase             : false,
                            bBuildSite                 : true,
                            bPublishSite               : params.PUBLISH_RELEASE,
                            bCreateGitHubRelease       : params.PUBLISH_RELEASE,
                            bRequireExplicitReleaseTag : true,
                            provisionalBuildDisplayName: "#${env.BUILD_NUMBER ?: '0'} Release_Publish queued",
                            latestQueuedBuildLockName  : 'kano-agent-backlog-skill-release-publish',
                        ] + upstreamConfig
                    )
                }
            }
        }
    }
}
