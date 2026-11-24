/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

'use client';

/**
 * @format
 *
 * OSS auth adapter — a static, single-user identity with no SSO.
 *
 * Rebalancer Explorer is a read-only viewer; external deployments are expected
 * to put their own authentication / ingress in front of it. This provider
 * exposes the same surface as the internal @nest/intern-auth adapter
 * (lib/platform/fb/auth.tsx) so feature code is identical in both builds.
 * Swap in real OAuth/OIDC here if a self-hoster needs per-user identity.
 *
 * The Next.js middleware counterpart (createAuthMiddleware) lives in the
 * sibling, non-client module lib/platform/auth-middleware.ts.
 */

import React, {createContext, useContext} from 'react';

import {Box, Button, Paper, Typography} from '@mui/material';

export interface AuthUser {
  id: string;
  name: string;
  username: string;
  profile_pic_uri?: string;
}

export interface AuthState {
  user: AuthUser | null;
  loading: boolean;
  logout: () => void;
}

const OSS_USER_NAME = process.env.NEXT_PUBLIC_OSS_USER || 'Explorer User';

const STATIC_STATE: AuthState = {
  user: {
    id: 'oss-user',
    name: OSS_USER_NAME,
    username: OSS_USER_NAME.toLowerCase().replace(/\s+/g, '_'),
  },
  loading: false,
  logout: () => {},
};

const AuthContext = createContext<AuthState>(STATIC_STATE);

export function AuthProvider({
  children,
}: {
  // Accepted for API parity with the internal @nest/intern-auth InternAuthProvider.
  // The OSS build has a static identity and no login flow, so it is ignored here.
  loginPath?: string;
  children: React.ReactNode;
}) {
  return (
    <AuthContext.Provider value={STATIC_STATE}>{children}</AuthContext.Provider>
  );
}

export function useAuth(): AuthState {
  return useContext(AuthContext);
}

/**
 * Stand-in for @nest/intern-auth's CATDetectionPage. The OSS build has no login
 * flow (middleware is pass-through and identity is static), so this just points
 * the visitor back to the app.
 */
function AuthCard({
  title,
  children,
}: {
  title: string;
  children: React.ReactNode;
}) {
  return (
    <Box
      sx={{
        minHeight: '100vh',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        p: 2,
      }}>
      <Paper
        variant="outlined"
        sx={{width: '100%', maxWidth: 400, p: 3, textAlign: 'center'}}>
        <Typography variant="h6" component="h1" sx={{mb: 1}}>
          {title}
        </Typography>
        {children}
      </Paper>
    </Box>
  );
}

export function LoginPage() {
  return (
    <AuthCard title="Rebalancer Explorer">
      <Typography variant="body2" color="text.secondary" sx={{mb: 2}}>
        You are signed in.
      </Typography>
      <Button variant="contained" href="/">
        Continue
      </Button>
    </AuthCard>
  );
}

export function LoggedOutPage({
  loginPath = '/login',
  ButtonComponent = Button,
}: {
  loginPath?: string;
  ButtonComponent?: React.ElementType;
}) {
  const Btn = ButtonComponent;
  return (
    <AuthCard title="Signed out">
      <Btn variant="contained" href={loginPath} sx={{mt: 1}}>
        Sign in
      </Btn>
    </AuthCard>
  );
}
