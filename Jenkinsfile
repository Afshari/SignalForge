pipeline {
    agent any

    parameters {
        string(
            name: 'BRANCH',
            defaultValue: 'main',
            description: 'Branch to build and test'
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
                sh 'docker compose --profile test run --rm tests 2>&1 | tee test-results.log'
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