# GitHub Authentication Setup Guide

This guide explains how to set up authentication to push code from your local computer to this repository.

## Creating a Fine-Grained Personal Access Token

GitHub now recommends using fine-grained personal access tokens for better security. These tokens allow you to grant specific permissions to specific repositories.

### Step 1: Navigate to Token Settings

1. Log in to your GitHub account
2. Click on your profile picture in the top-right corner
3. Go to **Settings**
4. In the left sidebar, scroll down and click on **Developer settings** (near the bottom)
5. Click on **Personal access tokens**
6. Select **Fine-grained tokens**

### Step 2: Generate New Token

1. Click the **Generate new token** button
2. Fill in the token details:
   - **Token name**: Give it a descriptive name (e.g., "LLVM Project - Local Development")
   - **Expiration**: Choose an expiration period (recommended: 90 days for security)
   - **Description**: Optional description of what this token is for
   - **Resource owner**: Select the repository owner (shanjare2002)

### Step 3: Configure Repository Access

1. Under **Repository access**, select **Only select repositories**
2. Choose this repository: `shanjare2002/llvm-project`

### Step 4: Set Repository Permissions

Under **Repository permissions**, grant the following permissions:
- **Contents**: Read and write (required for pushing code)
- **Metadata**: Read-only (automatically selected)
- **Pull requests**: Read and write (if you plan to create or update PRs)

### Step 5: Generate and Save Token

1. Click **Generate token** at the bottom
2. **IMPORTANT**: Copy the token immediately! GitHub will only show it once
3. Store it securely (e.g., in a password manager)

## Configuring Git with Your Token

### Option 1: HTTPS Clone with Token Authentication

When you clone or push to a repository via HTTPS, Git will prompt for credentials:

```bash
# Clone the repository
git clone https://github.com/shanjare2002/llvm-project.git

# When prompted:
# Username: your-github-username
# Password: paste-your-token-here
```

### Option 2: Configure Git Credential Helper

To avoid entering your token repeatedly, use Git's credential helper:

#### On Linux:
```bash
# Store credentials in memory for 15 minutes (900 seconds)
git config --global credential.helper cache

# Or store credentials permanently (less secure)
git config --global credential.helper store
```

#### On macOS:
```bash
# Use macOS Keychain
git config --global credential.helper osxkeychain
```

#### On Windows:
```bash
# Use Windows Credential Manager
git config --global credential.helper manager
```

### Option 3: Use Personal Access Token in Remote URL

You can embed the token in the remote URL (not recommended for security reasons):

```bash
git remote set-url origin https://YOUR_TOKEN@github.com/shanjare2002/llvm-project.git
```

**Warning**: This stores your token in plain text in `.git/config`

## Testing Your Setup

After configuring authentication, test it by making a small change:

```bash
# Navigate to your repository
cd llvm-project

# Create a new branch
git checkout -b test-authentication

# Make a small change (e.g., update a comment)
echo "# Test" >> test.txt

# Commit and push
git add test.txt
git commit -m "Test: Verify authentication setup"
git push origin test-authentication
```

If the push succeeds without errors, your authentication is configured correctly!

## Troubleshooting

### "Authentication failed" Error

- Verify your token hasn't expired
- Check that the token has the correct permissions (Contents: Read and write)
- Ensure you're using the token as your password, not your GitHub password

### "Permission denied" Error

- Verify the token is associated with the correct repository
- Check that repository access includes `shanjare2002/llvm-project`
- Ensure the token has write permissions for Contents

### Token Lost or Compromised

If you lose your token or suspect it's been compromised:
1. Go back to GitHub Settings > Developer settings > Personal access tokens
2. Find the token and click **Revoke**
3. Generate a new token following the steps above

## Security Best Practices

1. **Never commit tokens to the repository** - Always keep tokens secure
2. **Use expiration dates** - Regularly rotate tokens (every 90 days recommended)
3. **Grant minimal permissions** - Only give the permissions you actually need
4. **Revoke unused tokens** - Delete tokens you're no longer using
5. **Use credential helpers** - Avoid storing tokens in plain text

## Additional Resources

- [GitHub: Creating a personal access token](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/creating-a-personal-access-token)
- [GitHub: About fine-grained personal access tokens](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/managing-your-personal-access-tokens#about-fine-grained-personal-access-tokens)
- [Git Credential Storage](https://git-scm.com/docs/gitcredentials)

## Getting Help

If you continue to experience issues:
- Check the [LLVM Discourse forums](https://discourse.llvm.org/)
- Join the [LLVM Discord chat](https://discord.gg/xS7Z362)
- Review the [Contributing to LLVM](https://llvm.org/docs/Contributing.html) guide
