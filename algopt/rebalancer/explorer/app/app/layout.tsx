import type {Metadata} from 'next';
import InitColorSchemeScript from '@mui/material/InitColorSchemeScript';
import './globals.css';
import {ConsoleBridge} from './ConsoleBridge';
import {MuiThemeProvider} from './MuiThemeProvider';

// The icon is app/icon.svg (with app/favicon.ico as the fallback): the
// design language's product mark.
export const metadata: Metadata = {
  title: 'Rebalancer Explorer',
  description: 'Explore and analyze Rebalancer runs',
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    // The color-scheme script sets data-theme before hydration.
    <html lang="en" suppressHydrationWarning>
      <body className="antialiased">
        <InitColorSchemeScript attribute="data-theme" defaultMode="light" />
        <ConsoleBridge />
        <MuiThemeProvider>{children}</MuiThemeProvider>
      </body>
    </html>
  );
}
