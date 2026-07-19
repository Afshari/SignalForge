pipeline {
    agent any

    parameters {
        gitParameter(
            name: 'BRANCH',
            type: 'PT_BRANCH',
            defaultValue: 'main',
            description: 'Select branch to build and test',
            branchFilter: 'origin/(.*)',
            selectedValue: 'DEFAULT',
            sortMode: 'DESCENDING_SMART'
        )
    }


    stages {
        stage('Checkout') {
            steps {
                git branch: "${params.BRANCH}",
                    url: 'https://github.com/Afshari/SignalForge.git'
            }
        }

        stage('Build Image') {
            steps {
                sh 'docker compose build'
            }
        }

        stage('Test') {
            steps {
                timeout(time: 5, unit: 'MINUTES') {
                    sh 'docker compose --profile test run --rm tests 2>&1 | tee test-results.log'
                }
            }
        }
    }

    post {
        always {
            sh 'docker compose down --volumes || true'
            archiveArtifacts artifacts: 'test-results.log', allowEmptyArchive: true
        }
        success {
            echo 'Build and tests passed!'
        }
        failure {
            echo 'Build or tests failed!'
        }
    }
}